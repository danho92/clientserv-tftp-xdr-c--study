#include <rpc/xdr.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tftp.h"
#include "xdr_udp_utils.h"


void server_loop();
void process_WRQ(const char* filename);
void process_RRQ(const char* filename);

/* queste variabili sono usate da xdr_udp_utils, vanno usate qui solo in  *
 * casi eccezionali (es. inizializzazione e prima recvfrom() )            */
XDR in_xdrs;
char in_buff[MAX_RAW_MSG_SIZE];
XDR out_xdrs;
char out_buff[MAX_RAW_MSG_SIZE];

/* stderr non disponibile (flag richiesta dal modulo xdr_udp_utils) */
bool_t USE_STDERR = FALSE;


/**
 *  MAIN
**/
int main(int argc, char* argv[]) {
  pid_t pid;
  struct sockaddr_in cli_addr;
  socklen_t cli_addr_len;

  /* preparazione alla chiamata recvfrom */
  cli_addr_len = sizeof(cli_addr);
  memset(&cli_addr, 0x00, cli_addr_len);

  /* ricezione del primo messaggio */
  if (recvfrom(0, in_buff, MAX_BLOCK_SIZE, 0,
      (struct sockaddr*)&cli_addr, &cli_addr_len) <= 0) {
    exit(EXIT_FAILURE);
  }

  pid = fork();
  if (pid < 0) {
    exit(EXIT_FAILURE);
  }
  if (pid == 0) {
    /* CHILD */
    int sock;
    struct sockaddr_in srv_addr;

    /* creazione del socket di comunicazione */
    sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (errno != 0) {
      exit(EXIT_FAILURE);
    }

    /* bind del com socket ad una porta effimera e "connessione" al client */
    memset(&srv_addr, 0x00, sizeof(srv_addr));
    srv_addr.sin_family       = PF_INET;
    srv_addr.sin_port         = PORT_ANY;
    srv_addr.sin_addr.s_addr  = INADDR_ANY;
    if ( (bind(sock, (struct sockaddr*)&srv_addr, sizeof(srv_addr)) == -1) ||
         (connect(sock, (struct sockaddr*)&cli_addr, cli_addr_len) == -1)) {
      exit(EXIT_FAILURE);
    }

    /* dup() su stdin, stdout, e stderr */
    if ((dup2(sock, 0) != 0) ||
        (dup2(sock, 1) != 1) ||
        (dup2(sock, 2) != 2) ) {
      exit(EXIT_FAILURE);
    }

    /* init degli stream XDR */
    xdrmem_create(&in_xdrs,  in_buff,  MAX_RAW_MSG_SIZE, XDR_DECODE);
    xdrmem_create(&out_xdrs, out_buff, MAX_RAW_MSG_SIZE, XDR_ENCODE);

    /* passaggio del controllo alla routine di servizio TFTP */
    server_loop();
    exit(EXIT_SUCCESS);
  }
  /* PARENT */
  exit(EXIT_SUCCESS);
}


/**
 *  SERVER LOOP
**/
void server_loop() {
  msg_t msg;
  int try;
  bool_t error;

  /* il primo messaggio e' gia' nel buffer associato allo stream XDR */
  if (decode_msg(&msg) == FALSE) {
    err_rep(1, ILL_OP_TFTP, ERR_NOT_MSG);
    error = TRUE;
  }
  else {
    error = FALSE;
  }

  while (error == FALSE) {
    /* processamento del messaggio */
    switch (msg.op) {
      case REQ:
        if (msg.msg_t_u.req.reqtype == WRQ) {
          process_WRQ(msg.msg_t_u.req.filename);
        }
        else {
          process_RRQ(msg.msg_t_u.req.filename);
        }
        break;
      case ERR:
        error = TRUE;
        break;
      default:
        err_rep(1, ILL_OP_TFTP, ERR_NOT_REQ);
        error = TRUE;
        break;
    }
    xdr_free((xdrproc_t)xdr_msg_t, (char*)&msg);

    /* recupero di un nuovo messaggio */
    try = 0;
    do {
      switch (read_msg(0, &msg)) {
        case XDR_OK:
          try = 0;
          break;
        case XDR_FAIL:
          err_rep(1, ILL_OP_TFTP, ERR_NOT_MSG);
          error = TRUE;
          break;
        case RET_TIMEOUT:
          try += 1;
          break;
      }
    } while ((try > 0) && (try < MAX_TRY_COUNT) && (error == FALSE));

    /* nessun errore, ma troppi tentativi */
    if ((error == FALSE) && (try == MAX_TRY_COUNT)) {
      err_rep(1, NOT_DEFINED, ERR_TIMEOUTS);
      error = TRUE;
    }
  }

} /* END OF: server_loop() */


/**
 *  PROCESS WRQ
**/
void process_WRQ(const char* filename) {
  FILE* fout = fopen(filename, "wb");
  if (fout == NULL) {
    err_rep(1, ACCESS_VIOLATION, ERR_FOPEN);
    return;
  }

  /* ack0 = TRUE perche' il client deve sapere che REQ e' stato accettato */
  get_file(0, 1, fout, TRUE);
  fclose(fout);
}


/**
 *  PROCESS RRQ
**/
void process_RRQ(const char* filename) {
  FILE* fin = fopen(filename, "rb");
  if (fin == NULL) {
    err_rep(1, ACCESS_VIOLATION, ERR_FOPEN);
    return;
  }

  /* ack0 = FALSE perche', lato client, l'ACK #0 e' il messaggio REQ */
  put_file(0, 1, fin, FALSE);
  fclose(fin);
}


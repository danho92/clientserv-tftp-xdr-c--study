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
    exit(EXIT_FAILURE); // "initial recvfrom() gone wrong."
  }

  pid = fork();
  if (pid < 0) {
    exit(EXIT_FAILURE); // "fork() error."
  }
  if (pid == 0) {
    /* CHILD */
    int sock;
    struct sockaddr_in srv_addr;

    /* creazione del socket di comunicazione */
    sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (errno != 0) {
      exit(EXIT_FAILURE); // "couldn't create com socket."
    }

    /* bind e "connessione" del socket */
    memset(&srv_addr, 0x00, sizeof(srv_addr));
    srv_addr.sin_family       = PF_INET;
    srv_addr.sin_port         = PORT_ANY;
    srv_addr.sin_addr.s_addr  = INADDR_ANY;

    if (bind(sock, (struct sockaddr*)&srv_addr, sizeof(srv_addr)) == -1) {
      exit(EXIT_FAILURE); // "couldn't bind com socket."
    }
    if (connect(sock, (struct sockaddr*)&cli_addr, cli_addr_len) == -1) {
      exit(EXIT_FAILURE); // "couldn't UDP-connect com socket."
    }

    /* dup() su stdin, stdout, e stderr */
    if ((dup2(sock, 0) != 0) ||
        (dup2(sock, 1) != 1) ||
        (dup2(sock, 2) != 2) ) {
      exit(EXIT_FAILURE); // "couldn't dup() com socket on standard streams."
    }

    /* init degli stream xdr */
    xdrmem_create(&in_xdrs,  in_buff,  MAX_RAW_MSG_SIZE, XDR_DECODE);
    xdrmem_create(&out_xdrs, out_buff, MAX_RAW_MSG_SIZE, XDR_ENCODE);

    /* passaggio del controllo al server TFTP */
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

  try = 0;

  /* decodifica del primo messaggio. se valido, si salta la fase di fetch */
  memset(&msg, 0x00, sizeof(msg));
  if (xdr_msg_t(&in_xdrs, &msg) == TRUE) {
    goto CHECK_REQ;
  }

  while (try < MAX_TRY_COUNT) {
    if (read_msg(0, &msg) == XDR_OK) {
      /* messaggio valido */
      CHECK_REQ:
      if (msg.op == REQ) {
        /* e' una richiesta */
        if (msg.msg_t_u.req.reqtype == WRQ) {
          process_WRQ(msg.msg_t_u.req.filename);
        }
        else {
          process_RRQ(msg.msg_t_u.req.filename);
        }
        try = 0;
      }
      /* free del messaggio (non e' piu' necessario) */
      xdr_free((xdrproc_t)xdr_msg_t, (char*)&msg);
    }
    try += 1;
  }

  /* il client probabilmente non ricevera' questo messaggio.            *
   * in ogni caso terminera' alla prossima richiesta, per un errore di  *
   * invio (host unreachable) (testato)                                 */
  err_rep(1, NOT_DEFINED, "timeout - type faster!");

  return;
}


/**
 *  PROCESS WRQ
**/
void process_WRQ(const char* filename) {
  FILE* fout;

  fout = fopen(filename, "wb");
  if (fout == NULL) {
    err_rep(1, ACCESS_VIOLATION, "opening file");
    return;
  }

  get_file(0, 1, fout, TRUE);

  fclose(fout);
}


/**
 *  PROCESS RRQ
**/
void process_RRQ(const char* filename) {
  FILE* fin;

  fin = fopen(filename, "rb");
  if (fin == NULL) {
    err_rep(1, ACCESS_VIOLATION, "opening file");
    return;
  }

  put_file(0, 1, fin, FALSE);

  fclose(fin);
}


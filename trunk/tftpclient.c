#include <arpa/inet.h>
#include <rpc/xdr.h>
#include <sys/socket.h>

#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "tftp.h"
#include "xdr_udp_utils.h"


#define MAX_LINE_LEN        (512)


bool_t parse_req(char* line, msg_t* msg, char** localfilename);
void peek_connect(int fd);
void process_PUT(int fd, const char* filename);
void process_GET(int fd, const char* filename);

/* queste variabili sono usate da xdr_udp_utils, vanno usate qui solo in  *
 * casi eccezionali (es. inizializzazione e prima recvfrom() )            */
XDR in_xdrs;
char in_buff[MAX_PAYLOAD_SIZE];
XDR out_xdrs;
char out_buff[MAX_PAYLOAD_SIZE];

bool_t USE_STDERR = TRUE;


/**
 *  CLIENT MAIN
**/
int main(int argc, char* argv[]) {
  long n;
  char *end_ptr;
  int sock;
  struct sockaddr_in srv_addr, cli_addr;
  char line[MAX_LINE_LEN];
  int first;

  /* check degli argomenti e inizializzazioni */
  if (argc < 3) {
    errx(EXIT_FAILURE, "not enough arguments.");
  }
  memset(&srv_addr, 0x00, sizeof(srv_addr));
  srv_addr.sin_addr.s_addr  = inet_addr(argv[1]);
  if (srv_addr.sin_addr.s_addr == INADDR_NONE) {
    errx(EXIT_FAILURE, "%s is not a valid network address.", argv[1]);
  }
  n = strtol(argv[2], &end_ptr, 0);
  if (   (end_ptr == argv[2])
      || (*end_ptr != '\0')
      || (n <= 0)
      || (n > 65535) ) {
    errx(EXIT_FAILURE, "%s is not a valid port.", argv[2]);
  }
  srv_addr.sin_port         = htons((u_short)n);
  srv_addr.sin_family       = PF_INET;

  /* creazione del socket di comunicazione */
  sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (errno != 0) {
    err(EXIT_FAILURE, "couldn't create com socket.");
  }

  /* bind del socket */
  memset(&cli_addr, 0x00, sizeof(cli_addr));
  cli_addr.sin_family       = PF_INET;
  cli_addr.sin_addr.s_addr  = INADDR_ANY;
  cli_addr.sin_port         = PORT_ANY;
  bind(sock, (struct sockaddr*)&cli_addr, sizeof(cli_addr));
  if (errno != 0) {
    err(EXIT_FAILURE, "couldn't bind com socket.");
  }

  /* init degli stream xdr */
  xdrmem_create(&in_xdrs,  in_buff,  MAX_PAYLOAD_SIZE, XDR_DECODE);
  xdrmem_create(&out_xdrs, out_buff, MAX_PAYLOAD_SIZE, XDR_ENCODE);

  /* main loop */
  puts("Hello. Commands are:\n"
      "\t\"put <localfile> <remotefile>\" (<remotefile> will be OVERWRITTEN!)\n"
      "\t\"get <localfile> <remotefile>\" (<localfile> will be OVERWRITTEN!)");
  first = 1;
  while (fgets(line, MAX_LINE_LEN, stdin) != NULL) {
    msg_t msg;
    char *localfilename;
    if (parse_req(line, &msg, &localfilename) == TRUE) {
      if (first == 1) {
        first = 0;
        sendto_msg(sock, &srv_addr, &msg);
        peek_connect(sock);
      }
      else {
        write_msg(sock, &msg);
      }
      /* gestione dell'operazione */
      if (msg.msg_t_u.req.reqtype == WRQ) {
        /* process WRQ*/
        FILE* fin;
        fin = fopen(localfilename, "rb");
        if (fin == NULL) {
          fprintf(stderr, "local error: couldn't open %s\n", localfilename);
        }
        else {
          /* si passa ack0 = TRUE, in quanto il server risponde con ACK #0 */
          put_file(sock, sock, fin, TRUE);
          fclose(fin);
        }
      }
      else {
        /* process RRQ */
        FILE* fout;
        fout = fopen(localfilename, "wb");
        if (fout == NULL) {
          fprintf(stderr, "local error: couldn't open %s\n", localfilename);
        }
        else {
          /* si passa ack0 = FALSE, e' gia' implicito nel messaggio di richiesta */
          get_file(sock, sock, fout, FALSE);
          fclose(fout);
        }
      }
      puts("operation complete");
    }
    else {
      fprintf(stderr, "local error: command not recognized\n");
    }
  }
  exit(EXIT_SUCCESS);
}


/**
 *  PARSE REQUEST
**/
bool_t parse_req(char* line, msg_t* msg, char** localfilename) {
  char* p;
  char* p1;

  /* operazione: "put" o "get" (ignorando maiuscole/minuscole) */
  p = strtok(line, " ");
  if ( (p == NULL) ||
       ((strcasecmp(p, "put") != 0) && (strcasecmp(p, "get") != 0)) ){
    return FALSE;
  }
  msg->op = REQ;
  msg->msg_t_u.req.reqtype = (strcasecmp(p, "put") == 0)? WRQ:RRQ;

  /* estrazione filename locale */
  p = strtok(NULL, " ");
  if ((p == NULL) || (strlen(p) <= 0)) {
    return FALSE;
  }
  *localfilename = p;

  /* estrazione filename remoto (e rimozione newline) */
  p = strtok(NULL, " ");
  if ((p == NULL) || (strlen(p) <= 0)) {
    return FALSE;
  }
  p1 = strrchr(p, '\n');
  if (p1 != NULL) {
    *p1 = '\0';
  }
  msg->msg_t_u.req.filename = p;
  msg->msg_t_u.req.mode = "octet";

  return TRUE;
}


/**
 *  PEEK AND CONNECT
**/
void peek_connect(int fd) {
  struct sockaddr_in addr;
  socklen_t addr_len = sizeof(addr);

  memset(&addr, 0x00, addr_len);
  if (!( (recvfrom(fd, in_buff, 1, MSG_PEEK, (struct sockaddr*)&addr, &addr_len) == 1)
         && (connect(fd, (struct sockaddr*)&addr, addr_len) == 0) )) {
    err(EXIT_FAILURE, "performing peek and connect");
  }
}


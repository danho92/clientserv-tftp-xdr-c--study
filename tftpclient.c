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


void client_loop(int sock, struct sockaddr_in* first_addr);
bool_t parse_req(char* line, msg_t* msg, char** localfilename);
bool_t wait_reply(int sock, bool_t first);

/* queste variabili sono usate da xdr_udp_utils, vanno usate qui solo in  *
 * casi eccezionali (es. inizializzazione e prima recvfrom() )            */
XDR in_xdrs;
char in_buff[MAX_RAW_MSG_SIZE];
XDR out_xdrs;
char out_buff[MAX_RAW_MSG_SIZE];

bool_t USE_STDERR = TRUE;


/**
 *  CLIENT MAIN
**/
int main(int argc, char* argv[]) {
  long n;
  char *end_ptr;
  int sock;
  struct sockaddr_in srv_addr, cli_addr;

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
  xdrmem_create(&in_xdrs,  in_buff,  MAX_RAW_MSG_SIZE, XDR_DECODE);
  xdrmem_create(&out_xdrs, out_buff, MAX_RAW_MSG_SIZE, XDR_ENCODE);

  /* passaggio del controllo al client TFTP */
  client_loop(sock, &srv_addr);

  exit(EXIT_SUCCESS);
}


/**
 *  CLIENT LOOP
**/
void client_loop(int sock, struct sockaddr_in* first_addr) {
  char line[MAX_LINE_LEN];
  bool_t first;

  puts("Hello. Commands:\n"
      "\t\"put <localfile> <remotefile>\" (<remotefile> will be OVERWRITTEN!)\n"
      "\t\"get <localfile> <remotefile>\" (<localfile> will be OVERWRITTEN!)\n"
      "Command ?");

  first = TRUE;
  while (fgets(line, MAX_LINE_LEN, stdin) != NULL) {
    msg_t msg;
    char *localfilename;

    if (parse_req(line, &msg, &localfilename) == TRUE) {
      FILE *localfile;
      int try;

      /* controllo locale della richiesta */
      localfile = fopen(localfilename, (msg.msg_t_u.req.reqtype == WRQ)? "rb":"wb");
      if (localfile == NULL) {
        fprintf(stderr, "local error: couldn't open %s\n", localfilename);
        puts("Command ?");
        continue;
      }

      /* tentativi di invio della richiesta */
      try = 0;
      do {
        /* invio della richiesta */
        if (first == TRUE) {
          sendto_msg(sock, first_addr, &msg);
        }
        else {
          write_msg(sock, &msg);
        }

        /* attesa di una risposta con MSG_PEEK (e connect se first == TRUE) */
        if (wait_reply(sock, first) == TRUE) {
          try = 0;
          first = FALSE;
          if (msg.msg_t_u.req.reqtype == WRQ) {
            put_file(sock, sock, localfile, TRUE);
          }
          else {
            get_file(sock, sock, localfile, FALSE);
          }
          puts("operation complete");
        }
        else {
          try += 1;
        }
      } while ((try > 0) && (try < MAX_TRY_COUNT));
      fclose(localfile);

      /* troppi timeout */
      if (try == MAX_TRY_COUNT) {
        fprintf(stderr, "error: no reply from server\n");
      }

    }
    else {
      fprintf(stderr, "local error: command not recognized\n");
    }

    puts("Command ?");
  }
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
 *  WAIT REPLY (AND CONNECT IF NEEDED)
**/
bool_t wait_reply(int sock, bool_t first) {
  fd_set fds;
  int rdy_count;
  struct timeval timeout = {TIMEOUT, 0};
  FD_ZERO(&fds);
  FD_SET(sock, &fds);

  rdy_count = select(sock + 1, &fds, NULL, NULL, &timeout);

  if (rdy_count == 0) return FALSE;
  if (rdy_count != 1) err(EXIT_FAILURE, "select() error");

  if (first == TRUE) {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    memset(&addr, 0x00, addr_len);
    if (!( (recvfrom(sock, in_buff, 1, MSG_PEEK, (struct sockaddr*)&addr, &addr_len) == 1)
           && (connect(sock, (struct sockaddr*)&addr, addr_len) == 0) )) {
      err(EXIT_FAILURE, "'peeking' server reply and connecting");
    }
  }
  else if (recvfrom(sock, in_buff, 1, MSG_PEEK, NULL, NULL) != 1) {
      err(EXIT_FAILURE, "'peeking' server reply");
  }
  return TRUE;
}


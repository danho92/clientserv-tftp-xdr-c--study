#include <sys/select.h>
#include <sys/socket.h>

#include <string.h>
#include <unistd.h>

#include "tftp.h"
#include "xdr_udp_utils.h"


/* prototipo del metodo di encoding usato da write_msg e sendto_msg */
u_int encode_msg(msg_t* msg);


/**
 *  READ TFTP PDU | DECODE
**/
bool_t read_msg(int fd, msg_t* msg) {
  fd_set fds;
  int rdy_count;
  struct timeval timeout = {TIMEOUT, 0};

  FD_ZERO(&fds);
  FD_SET(fd, &fds);
  rdy_count = select(fd + 1, &fds, NULL, NULL, &timeout);
  switch (rdy_count) {
    case 0:
      return FALSE;
    case 1:
      if ( (read(fd, in_buff, MAX_PAYLOAD_SIZE) > 0) &&
           (xdr_setpos(&in_xdrs, 0) == TRUE) ) {
        /* lettura e reset dello stream xdr ok */
        break;
      }
    default:
      /* errore fatale read o select */
      exit(EXIT_FAILURE);
      break;
  }
  /* tentativo di conversione XDR del messaggio */
  memset(msg, 0x00, sizeof(msg_t));
  return (xdr_msg_t(&in_xdrs, msg) == TRUE);
}


/**
 *  ENCODE TFTP MESSAGE
**/
u_int encode_msg(msg_t* msg) {
  u_int size;
  if (! ((xdr_setpos(&out_xdrs, 0) == TRUE) &&
         (xdr_msg_t(&out_xdrs, msg) == TRUE)) ) {
    exit(EXIT_FAILURE);
  }
  return xdr_getpos(&out_xdrs) + 1;
}

/**
 *  ENCODE TFTP MESSAGE | WRITE
**/
void write_msg(int fd, msg_t* msg) {
  u_int size = encode_msg(msg);
  if (write(fd, out_buff, size) != size) {
    exit(EXIT_FAILURE);
  }
}

/**
 *  ENCODE TFTP MESSAGE | SENDTO
**/
void sendto_msg(int fd, struct sockaddr_in* srv_addr, msg_t* msg) {
  u_int size = encode_msg(msg);
  if (sendto(fd, out_buff, size, 0,
              (struct sockaddr*)srv_addr, sizeof(struct sockaddr_in)) != size) {
    exit(EXIT_FAILURE);
  }
}


/**
 *  WRITE ACK MESSAGE
**/
void write_ACK(int fd, blockn_t blocknum) {
  msg_t msg;
  msg.op = ACK;
  msg.msg_t_u.ack.blocknum = blocknum;
  write_msg(fd, &msg);
}

/**
 *  WRITE ERROR MESSAGE
**/
void write_ERR(int fd, errcode_t errcode, char* errstr) {
  msg_t msg;
  msg.op = ERR;
  msg.msg_t_u.err.errcode = errcode;
  msg.msg_t_u.err.errstr = errstr;
  write_msg(fd, &msg);
}


/**
 *  ERROR REPORT
**/
void err_rep(FILE* local, int remote_fd, errcode_t errcode, char* errstr) {
  if (local != NULL) {
    fprintf(local, "local error: %s\n", errstr);
  }
  write_ERR(remote_fd, errcode, errstr);
}


/**
 *  GET FILE FROM NETWORK
**/
void get_file(int in, int out, FILE* ferr, FILE* fout, bool_t ack0) {
  msg_t msg;
  dat_t* dat;
  blockn_t blocknum;
  int try;
  bool_t error;

  if (ack0 == TRUE) {
    write_ACK(out, 0);
  }

  error = FALSE;
  blocknum = 1;
  try = 0;
  while ((try < MAX_TRY_COUNT) && (error == FALSE)) {
    if (read_msg(in, &msg) == TRUE) {
      /* messaggio TFTP valido */
      switch (msg.op) {
        case DAT:       // DAT
          dat = &(msg.msg_t_u.dat);
          switch (blocknum - (dat->blocknum)) {
            case 0:      // DAT esatto
              if (fwrite(dat->payload.payload_val, dat->payload.payload_len, 1, fout) != 1) {
                err_rep(ferr, out, ACCESS_VIOLATION, "writing output file");
                error = TRUE;
              }
              write_ACK(out, blocknum);
              if (dat->payload.payload_len < MAX_BLOCK_LEN) {
                xdr_free((xdrproc_t)xdr_msg_t, (char*)&msg);
                return; // PUNTO DI USCITA 'CANONICO'
              }
              blocknum += 1;
              try = -1;
              break;
            case 1:  // DAT precedente
              write_ACK(out, blocknum - 1);
              try = -1;
              break;
            default:            // DAT errato
              err_rep(ferr, out, ILL_OP_TFTP, "bad DAT");
              error = TRUE;
              break;
          }
          break;
        case ERR:       // ERR
          if (ferr != NULL) {
            fprintf(ferr, "remote error: %s\n", msg.msg_t_u.err.errstr);
          }
          error = TRUE;
          break;
        default:        // ALTRO
          err_rep(ferr, out, ILL_OP_TFTP, "not DAT");
          error = TRUE;
          break;
      }
      xdr_free((xdrproc_t)xdr_msg_t, (char*)&msg);
    }
    try += 1;
  }

  /* niente errori, ma troppi tentativi */
  if ((error == FALSE) && (try == MAX_TRY_COUNT)) {
    err_rep(ferr, out, NOT_DEFINED, "timeout");
  }

}




/**
 *  PUT FILE ON NETWORK
**/
void put_file(int in, int out, FILE* ferr, FILE* fin, bool_t ack0) {
  msg_t out_msg;
  dat_t* dat;
  blockn_t blocknum;
  int size;
  int try;
  bool_t error;

  out_msg.op = DAT;
  dat = &(out_msg.msg_t_u.dat);
  dat->payload.payload_val = NULL;

  error = FALSE;
  /* se e' richiesta la ricezione di ACK #0 occorre inizializzare in maniera  *
   * differente alcune variabili, e saltare dentro al ciclo di ricezione      */
  if (ack0 == TRUE) {
    size = MAX_BLOCK_LEN;
    blocknum = 0;
    try = 0;
    goto READ;
  }
  blocknum = 1;

  do {
    char buff[MAX_BLOCK_LEN];
    msg_t in_msg;

    /* lettura del blocco dal file */
    size = fread(buff, 1, MAX_BLOCK_LEN, fin);
    if (size < 0) {
      err_rep(ferr, out, ACCESS_VIOLATION, "reading file");
      return;
    }
    /* preparazione dat */
    dat->blocknum = blocknum;
    dat->payload.payload_len = size;
    dat->payload.payload_val = malloc(size);
    if (dat->payload.payload_val == NULL) {
      err_rep(ferr, out, ACCESS_VIOLATION, "allocating memory");
      /* questo e' un errore grave, si decide di uscire bruscamente */
      exit(EXIT_FAILURE);
    }
    memcpy(dat->payload.payload_val, buff, size);

    /* tentativi di invio */
    try = 0;
    while (try < MAX_TRY_COUNT) {
      write_msg(out, &out_msg);
      READ:
      if (read_msg(in, &in_msg) == TRUE) {
        /* messaggio TFTP valido */
        if ((in_msg.op == ACK) && (in_msg.msg_t_u.ack.blocknum == blocknum)) {
          /* messaggio atteso */
          blocknum += 1;
          break;  // niente xdr_free ma ACK non ha campi allocati dinamicamente
        }
        /* messaggio inatteso */
        else  if (in_msg.op == ERR) {
                if (ferr != NULL) {
                  fprintf(ferr, "remote error: %s\n", in_msg.msg_t_u.err.errstr);
                }
              }
              else {
                err_rep(ferr, out, ILL_OP_TFTP, "not ACK/bad ACK");
              }
        error = TRUE;
        xdr_free((xdrproc_t)xdr_msg_t, (char*)&in_msg);
        break;
      }
      /* si giunge qui per timeout o ricezione di messaggi non TFTP */
      try += 1;
    }

    /* niente errori, ma troppi tentativi */
    if ((error == FALSE) && (try == MAX_TRY_COUNT)) {
      error = TRUE;
      err_rep(ferr, out, NOT_DEFINED, "timeout");
    }

    /* free del payload; check del puntatore perche' il flag ack0 settato *
     * fa evitare la prima malloc()                                       */
    if (dat->payload.payload_val != NULL) {
      free(dat->payload.payload_val);
    }

  } while ((error == FALSE) && (size == MAX_BLOCK_LEN));
}


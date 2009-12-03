#include <sys/select.h>
#include <sys/socket.h>

#include <string.h>
#include <unistd.h>

#include "tftp.h"
#include "xdr_udp_utils.h"
#include <errno.h>


/**
 *  DECODE TFTP MESSAGE
**/
bool_t decode_msg(msg_t* msg) {
  if (xdr_setpos(&in_xdrs, 0) == FALSE) return FALSE;
  memset(msg, 0x00, sizeof(msg_t));
  return xdr_msg_t(&in_xdrs, msg);
}

/**
 *  READ TFTP MESSAGE | DECODE
**/
read_msg_ret_t read_msg(int fd, msg_t* msg) {
  fd_set fds;
  int rdy_count;
  struct timeval timeout = {TIMEOUT, 0};

  FD_ZERO(&fds);
  FD_SET(fd, &fds);
  rdy_count = select(fd + 1, &fds, NULL, NULL, &timeout);
  switch (rdy_count) {
    case 0:
      return RET_TIMEOUT;
    case 1:
      if (read(fd, in_buff, MAX_RAW_MSG_SIZE) > 0) {
        return (decode_msg(msg) == TRUE)? XDR_OK:XDR_FAIL;
      }
  }
  /* errore fatale read o select */
  exit(EXIT_FAILURE);
}


/**
 *  ENCODE TFTP MESSAGE
**/
u_int encode_msg(msg_t* msg) {
  u_int size;
  if (! ((xdr_setpos(&out_xdrs, 0) == TRUE) &&
         (xdr_msg_t(&out_xdrs, msg) == TRUE)) ) {
    /* errore fatale di encoding XDR */
    exit(EXIT_FAILURE);
  }
  return xdr_getpos(&out_xdrs) /*+ 1*/;
}

/**
 *  ENCODE TFTP MESSAGE | WRITE
**/
void write_msg(int fd, msg_t* msg) {
  u_int size = encode_msg(msg);
  if (write(fd, out_buff, size) != size) {
    /* errore fatale write */
    exit(EXIT_FAILURE);
  }
}

/**
 *  ENCODE TFTP MESSAGE | SENDTO
**/
void sendto_msg(int sock, struct sockaddr_in* srv_addr, msg_t* msg) {
  u_int size = encode_msg(msg);
  if (sendto(sock, out_buff, size, 0,
              (struct sockaddr*)srv_addr, sizeof(struct sockaddr_in)) != size) {
    /* errore fatale sendto */
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
void err_rep(int fd, errcode_t errcode, char* errstr) {
  if (USE_STDERR == TRUE) {
    fprintf(stderr, "Local error: %s\n", errstr);
  }
  write_ERR(fd, errcode, errstr);
}


/**
 *  GET FILE FROM NETWORK
**/
void get_file(int in, int out, FILE* fout, bool_t ack0) {
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
    switch (read_msg(in, &msg)) {
      /************************************************************************/
      case XDR_OK:                                      /* 1: messaggio TFTP  */
        switch (msg.op) {

          case DAT:                                     /* 1.1: messaggio DAT */
            dat = &(msg.msg_t_u.dat);
            switch (blocknum - (dat->blocknum)) {
              case 0:                                   /* 1.1-A: DAT giusto  */
               if (fwrite(dat->block.block_val, 1,
                          dat->block.block_len, fout) != dat->block.block_len) {
                  err_rep(out, ACCESS_VIOLATION, ERR_FWRITE);
                  error = TRUE;
                }
                else {
                  /* la scrittura e' andata a buon fine */
                  write_ACK(out, blocknum);
                  if (dat->block.block_len < MAX_BLOCK_SIZE) {
                    xdr_free((xdrproc_t)xdr_msg_t, (char*)&msg);
                    /* OPERAZIONE COMPLETATA CON SUCCESSO */
                    return;
                  }
                  try = 0;
                  blocknum += 1;
                }
                break;
              case 1:                                   /* 1.1-B: DAT preced. */
                write_ACK(out, blocknum - 1);
                /* l'ack precedente e' andato perso: aumento del try count */
                try += 1;
                break;
              default:                                  /* 1.1-C: DAT errato  */
                err_rep(out, ILL_OP_TFTP, ERR_BAD_DAT);
                error = TRUE;
                break;
            }
            break;

          case ERR:                                     /* 1.2: messaggio ERR */
            if (USE_STDERR == TRUE) {
              fprintf(stderr, "Remote error: %s\n", msg.msg_t_u.err.errstr);
            }
            error = TRUE;
            break;

          default:                                      /* 1.3: altro mess.   */
            err_rep(out, ILL_OP_TFTP, ERR_NOT_DAT);
            error = TRUE;
            break;
        }
        xdr_free((xdrproc_t)xdr_msg_t, (char*)&msg);
        break;
      /************************************************************************/
      case XDR_FAIL:                                    /* 2: mess. non TFTP  */
        err_rep(out, ILL_OP_TFTP, ERR_NOT_MSG);
        error = TRUE;
        break;
      /************************************************************************/
      case RET_TIMEOUT:                                 /* 3: select timeout  */
        try +=1;
        break;
    }
  }

  /* niente errori, ma troppi tentativi */
  if ((error == FALSE) && (try == MAX_TRY_COUNT)) {
    err_rep(out, NOT_DEFINED, ERR_TIMEOUTS);
  }
} /* END OF: get_file() */


/**
 *  PUT FILE ON NETWORK
**/
void put_file(int in, int out, FILE* fin, bool_t ack0) {
  msg_t out_msg;
  dat_t* dat;
  bool_t error;
  int size;
  blockn_t blocknum;
  int try;

  out_msg.op = DAT;
  dat = &(out_msg.msg_t_u.dat);
  dat->block.block_val = NULL;

  error = FALSE;
  try = 0;

  /* se e' richiesta la ricezione di ACK #0 occorre inizializzare in maniera  *
   * differente alcune variabili, e saltare dentro al ciclo di ricezione      */
  if (ack0 == TRUE) {
    size = MAX_BLOCK_SIZE;
    blocknum = 0;
    goto READ;
  }

  blocknum = 1;
  do {
    char buff[MAX_BLOCK_SIZE];
    msg_t in_msg;

    /* lettura del blocco dal file */
    size = fread(buff, 1, MAX_BLOCK_SIZE, fin);
    if (size < 0) {
      err_rep(out, ACCESS_VIOLATION, ERR_FREAD);
      return;
    }
    /* preparazione dat */
    dat->blocknum = blocknum;
    dat->block.block_len = size;
    dat->block.block_val = malloc(size);
    if (dat->block.block_val == NULL) {
      /* questo e' un errore grave, si esce bruscamente */
      exit(EXIT_FAILURE);
    }
    memcpy(dat->block.block_val, buff, size);

    /* tentativi di invio */
    do {
      write_msg(out, &out_msg);
      READ:
      switch (read_msg(in, &in_msg)) {
        /**********************************************************************/
        case XDR_OK:                                    /* 1: messaggio TFTP  */
          switch (in_msg.op) {

            case ACK:                                   /* 1.1: messaggio ACK */
              if (in_msg.msg_t_u.ack.blocknum == blocknum) {
                blocknum += 1;
                /* il set di try a 0 fa uscire dal ciclo di invio */
                try = 0;
              }
              else {
                err_rep(out, ILL_OP_TFTP, ERR_BAD_ACK);
                error = TRUE;
              }
              break;

            case ERR:                                   /* 1.2: messaggio ERR */
              if (USE_STDERR == TRUE) {
                fprintf(stderr, "Remote error: %s\n", in_msg.msg_t_u.err.errstr);
              }
              error = TRUE;
              break;

            default:                                    /* 1.3: altro mess.   */
              err_rep(out, ILL_OP_TFTP, ERR_NOT_ACK);
              error = TRUE;
              break;
          }
          xdr_free((xdrproc_t)xdr_msg_t, (char*)&in_msg);
          break;
        /**********************************************************************/
        case XDR_FAIL:                                  /* 2: mess. non TFTP  */
          err_rep(out, ILL_OP_TFTP, ERR_NOT_MSG);
          error = TRUE;
          break;
        /**********************************************************************/
        case RET_TIMEOUT:                               /* 3: select timeout  */
          try += 1;
          break;
      }
    } while ((try > 0) && (try < MAX_TRY_COUNT) && (error == FALSE));

    /* niente errori, ma troppi tentativi */
    if ((error == FALSE) && (try == MAX_TRY_COUNT)) {
      err_rep(out, NOT_DEFINED, ERR_TIMEOUTS);
      error = TRUE;
    }

    /* free del blocco; check del puntatore perche' il flag ack0 settato *
     * fa evitare la prima malloc()                                       */
    if (dat->block.block_val != NULL) {
      free(dat->block.block_val);
    }

  } while ((error == FALSE) && (size == MAX_BLOCK_SIZE));
} /* END OF: put_file() */


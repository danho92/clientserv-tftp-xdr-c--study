#ifndef _XDR_UDP_UTILS_H_
#define _XDR_UDP_UTILS_H_


#include <rpc/xdr.h>

#include <stdio.h>

#include "tftp.h"


#define MAX_RAW_MSG_SIZE    (8192)
#define TIMEOUT             (1)
#define MAX_TRY_COUNT       (16)
#define PORT_ANY            (htons(0))

#define ERR_TIMEOUTS        "too many timeouts"
#define ERR_NOT_MSG         "not TFTP message"
#define ERR_BAD_ACK         "bad TFTP ACK"
#define ERR_NOT_ACK         "not TFTP ACK"
#define ERR_BAD_DAT         "bad TFTP DAT"
#define ERR_NOT_DAT         "not TFTP DAT"
#define ERR_NOT_REQ         "not TFTP REQ"
#define ERR_FOPEN           "could not open file"
#define ERR_FREAD           "could not read from file"
#define ERR_FWRITE          "could not write to file"

/* gli stream xdr vanno inizializzati (con xdrmem_create) prima dell' *
 * utilizzo delle funzioni che lavorano con XDR                       */
extern XDR in_xdrs;
extern char in_buff[MAX_RAW_MSG_SIZE];
extern XDR out_xdrs;
extern char out_buff[MAX_RAW_MSG_SIZE];

/* TRUE se stderr e' disponibile */
extern bool_t USE_STDERR;


/* tipo di ritorno delle funzioni di ricezione e decoding a timeout */
typedef enum read_msg_ret {
  XDR_OK,
  XDR_FAIL,
  RET_TIMEOUT
} read_msg_ret_t;

/* ricezione e decoding a timeout (xdr_free a carico del destinatario) */
bool_t decode_msg(msg_t* msg);
read_msg_ret_t read_msg(int fd, msg_t* msg);

/* encoding e invio */
u_int encode_msg(msg_t* msg);
void write_msg(int fd, msg_t* msg);
void sendto_msg(int sock, struct sockaddr_in* srv_addr, msg_t* msg);

/* routine di error reporting remota (e locale se USE_STDERR = TRUE) */
void err_rep(int fd, errcode_t errcode, char* errstr);

/* shortcut per encoding e invio di messaggi ERR ed ACK */
void write_ERR(int fd, errcode_t errcode, char* errstr);
void write_ACK(int fd, blockn_t blocknum);

/* routine di invio/ricezione file (implementazione del protocollo TFTP) */
void get_file(int in, int out, FILE* fout, bool_t ack0);
void put_file(int in, int out, FILE* fin, bool_t ack0);


#endif /* _XDR_UDP_UTILS_H_ */

#ifndef _XDR_UDP_UTILS_H_
#define _XDR_UDP_UTILS_H_

#include <rpc/xdr.h>

#include <stdio.h>

#include "tftp.h"


#define MAX_RAW_MSG_SIZE    (8192)
#define TIMEOUT             (2)
#define MAX_TRY_COUNT       (16)
#define PORT_ANY            (htons(0))


/* gli stream xdr vanno inizializzati (con xdrmem_create) prima dell' *
 * utilizzo delle funzioni che lavorano con XDR                       */
extern XDR in_xdrs;
extern char in_buff[MAX_RAW_MSG_SIZE];
extern XDR out_xdrs;
extern char out_buff[MAX_RAW_MSG_SIZE];

/* TRUE se stderr e' disponibile */
extern bool_t USE_STDERR;


/* tipo del valore di ritorno della funzione read_msg() */
typedef enum read_msg_ret {
  XDR_OK,
  XDR_FAIL,
  RET_TIMEOUT
} read_msg_ret_t;


/* ricezione e deserializzazione (la xdr_free e' ovviamente a carico del  *
 * destinatario)                                                          */
read_msg_ret_t read_msg(int fd, msg_t* msg);

/* serializzazione e invio */
void write_msg(int fd, msg_t* msg);
void sendto_msg(int fd, struct sockaddr_in* srv_addr, msg_t* msg);

/* routine di error reporting */
void err_rep(int fd, errcode_t errcode, char* errstr);

/* serializzazione e invio per messaggi ERR ed ACK */
void write_ERR(int fd, errcode_t errcode, char* errstr);
void write_ACK(int fd, blockn_t blocknum);

/* routine di invio/ricezione file utili sia al server che al client */
void get_file(int in, int out, FILE* fout, bool_t ack0);
void put_file(int in, int out, FILE* fin, bool_t ack0);


#endif /* _XDR_UDP_UTILS_H_ */

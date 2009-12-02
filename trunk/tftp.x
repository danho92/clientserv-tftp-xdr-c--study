
const MAX_FILENAME_LEN  = 500;
const MAX_MODE_LEN      = 5;
const MAX_BLOCK_SIZE    = 512;
const MAX_ERRSTR_LEN    = 100;


enum op_t {
  REQ   = 0x01,
  DAT   = 0x03,
  ACK   = 0x04,
  ERR   = 0x05
};

enum reqt_t {
  RRQ   = 0x01,
  WRQ   = 0x02
};

/* facilita il cambio del tipo del dato usato per la numerazione dei blocchi */
typedef unsigned int blockn_t;

enum errcode_t {
  NOT_DEFINED         = 0x00,
  FILE_NOT_FOUND      = 0x01,
  ACCESS_VIOLATION    = 0x02,
  DISK_FULL           = 0x03,
  ILL_OP_TFTP         = 0x04,
  UNKNOWN_PORT        = 0x05,
  FILE_ALREADY_EXISTS = 0x06,
  NO_SUCH_USER        = 0x07
};


struct req_t {
  reqt_t      reqtype;
  string      filename<MAX_FILENAME_LEN>;
  string      mode<MAX_MODE_LEN>;
};

struct dat_t {
  blockn_t    blocknum;
  opaque      block<MAX_BLOCK_SIZE>;
};

struct ack_t {
  blockn_t    blocknum;
};

struct err_t {
  errcode_t   errcode;
  string      errstr<MAX_ERRSTR_LEN>;
};


union msg_t switch (op_t op) {
  case REQ:   req_t req;
  case DAT:   dat_t dat;
  case ACK:   ack_t ack;
  case ERR:   err_t err;
};


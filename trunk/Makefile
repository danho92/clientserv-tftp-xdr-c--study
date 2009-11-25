CFLAGS = -std=c99 -pedantic -Wextra -g
SRV_OBJS = tftpserver.o
CLI_OBJS = tftpclient.o
SHR_OBJS = tftp_xdr.o xdr_udp_utils.o

all: server client

server: $(SRV_OBJS) $(SHR_OBJS)
	$(CC) $(CFLAGS) -o tftpserver $(SRV_OBJS) $(SHR_OBJS)
tftpserver.o : tftpserver.c

client: $(CLI_OBJS) $(SHR_OBJS)
	$(CC) $(CFLAGS) -o tftpclient $(CLI_OBJS) $(SHR_OBJS)
tftpclient.o : tftpclient.c

tftp_xdr.o : tftp_xdr.c
xdr_udp_utils.o : xdr_udp_utils.c

clean:
	-rm tftpserver $(SRV_OBJS) tftpclient $(CLI_OBJS) $(SHR_OBJS)

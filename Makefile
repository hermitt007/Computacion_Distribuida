CC = gcc
CFLAGS = -Wall -O2

all: ViteS-clienteFTP

ViteS-clienteFTP: ViteS-clienteFTP.c connectTCP.c errexit.c connectsock.c
	$(CC) $(CFLAGS) -o ViteS-clienteFTP ViteS-clienteFTP.c connectTCP.c errexit.c connectsock.c

clean:
	rm -f ViteS-clienteFTP *.o


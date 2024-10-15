CC ?= gcc
CFLAGS ?= -g -Wall -Werror
LDFLAGS ?= -lrt -lpthread

ifdef CROSS_COMPILE
	CC ?= ${CROSS_COMPILE}gcc
endif

all: aesdsocket

aesdsocket: aesdsocket.o
		$(CC) $(CFLAGS) aesdsocket.o -o aesdsocket $(LDFLAGS)

aesdsocket.o:aesdsocket.c
		$(CC) $(CFLAGS) -c aesdsocket.c -o aesdsocket.o

clean:
	rm -f *.o aesdsocket *.elf *.map

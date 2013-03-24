CC=gcc -Wall -g


TARGETS=ipdwl

all: $(TARGETS)

ipdwl: ipdwl.c
	$(CC) -o ipdwl ipdwl.c -lcrypto

clean:
	rm -f *~ $(TARGETS)

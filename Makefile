.PHONY: all clean

CFLAGS=-Wall -Wextra -O2 -g

all: rcpar1 rcpar2 rcorder2 mtime

rcpar1: rcpar1.c
	cc ${CFLAGS} $? -o $@ -pthread

rcpar2: rcpar2.c

rcorder2: rcorder2.c

mtime: mtime.c

clean:
	rm -f rcpar1 rcpar2 rcorder2 mtime

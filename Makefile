.PHONY: all clean

CFLAGS=-Wall -Wextra -O2 -g

all: rcpar rcorder2 mtime

rcpar: rcpar.c

rcorder2: rcorder2.c

mtime: mtime.c

clean:
	rm -f rcpar rcorder2 mtime

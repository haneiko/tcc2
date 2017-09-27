.PHONY: all clean

CFLAGS=-Wall -Wextra -O2 -g

all: rcpar2 rcorder2 mtime

rcpar2: rcpar2.c

rcorder2: rcorder2.c

mtime: mtime.c

clean:
	rm -f rcpar2 rcorder2 mtime

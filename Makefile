.PHONY: all install clean
CFLAGS = -g -Wall -Wextra -O2
PROGS  = rcpar rcorder2 mtime

all: $(PROGS)
rcpar: rcpar.c
rcorder2: rcorder2.c
mtime: mtime.c
install:
	mv $(PROGS) /sbin/
clean:
	rm -f $(PROGS)

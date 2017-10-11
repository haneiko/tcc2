.PHONY: all install clean
CFLAGS = -g -Wall -Wextra -O2
PROGS  = rcpar rcorder2 mtime calc_stats

all: $(PROGS)
rcpar: rcpar.c
rcorder2: rcorder2.c
mtime: mtime.c
calc_stats: calc_stats.c
install:
	mv $(PROGS) /sbin/
clean:
	rm -f $(PROGS)

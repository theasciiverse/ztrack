# makefile for ztrack
#
# note that you need an ANSI C compiler;
# gcc is easiest if you've got it or can get it
CC=gcc

# flags for Linux/gcc, hack as required
# remove '-DHAVE_RANDOM' if your libc doesn't have 'random' and 'srandom'.
# add '-DNO_COLOUR' if you don't want colour or the colour support
# causes problems.
CFLAGS=-O2 -Wall -DHAVE_RANDOM -I/usr/include/ncurses #-DDEBUG

# if using ncurses:
LIBS=-lncurses

# bsd-derived is usually:
#LIBS=-lcurses -ltermcap

# sysv-derived is usually:
#LIBS=-lcurses -lterminfo

# change these if needed - they're used by 'make install'
BINDIR=/usr/games
MANDIR=/usr/man/man6

# don't edit below this line
#--------------------------------------------------------

OBJS=ztrack.o

all: ztrack

ztrack: $(OBJS)
	$(CC) $(CFLAGS) -o ztrack $(OBJS) $(LIBS)

install: ztrack
	install -s -m 511 ztrack $(BINDIR)
	install -m 444 ztrack.6 $(MANDIR)

clean:
	$(RM) *.o *~ ztrack

ztrack.o: ztrack.c carspr.h

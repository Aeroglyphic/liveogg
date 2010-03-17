#Makefile for liveogg, Laurent Raufaste
CC = gcc
CCFLAGS = -O3
CCLIBS = -logg -lvorbis -lvorbisenc

all: liveogg

clean:
	rm -f liveogg

install:
	cp liveogg /home/analogue/icecast/liveogg

liveogg: liveogg.c
	$(CC) $(CCFLAGS) $(CCLIBS) -o liveogg liveogg.c
	strip liveogg

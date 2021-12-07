CC ?= cc
CFLAGS = -I../../include -O0 -g
LFLAGS = -Wl,-rpath,../../o/debug -L../../o/debug -lgorilla -lm

default: tone-lowlevel tone-highlevel
tone-lowlevel: tone-lowlevel.c ../../o/debug/libgorilla.a
	$(CC) $(CFLAGS) -o tone-lowlevel tone-lowlevel.c $(LFLAGS)
tone-highlevel: tone-highlevel.c ../../o/debug/libgorilla.a
	$(CC) $(CFLAGS) -o tone-highlevel tone-highlevel.c $(LFLAGS)

../../o/debug/libgorilla.a:
	make -C ../..

clean:
	rm -f tone-lowlevel tone-highlevel

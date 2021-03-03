CC ?= cc
CFLAGS = -I../../include -Os -g
LFLAGS = -Wl,-rpath=../../o/debug -L../../o/debug -lgorilla -lm

tone: tone.c ../../o/debug/libgorilla.a
	$(CC) $(CFLAGS) $(LFLAGS) -o tone tone.c ../../o/debug/libgorilla.a

../../o/debug/libgorilla.a:
	make -C ../..

clean:
	rm -f tone

CC ?= cc
CFLAGS = -I../../include -Os -g
LFLAGS = -Wl,-rpath=../../o/debug -L../../o/debug -lgorilla -lm -lxaudio2_8 -lole32

tone: tone.c ../../o/debug/libgorilla.a
	$(CC) $(CFLAGS) -o tone tone.c ../../o/debug/libgorilla.a $(LFLAGS)

../../o/debug/libgorilla.a:
	make -C ../..

clean:
	rm -f tone

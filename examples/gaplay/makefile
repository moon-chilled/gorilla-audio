CC ?= cc
CFLAGS = -I../../include -Os -g
LFLAGS = -Wl,-rpath=../../o/debug -L../../o/debug -lgorilla
O := gaplay.o ../../contrib/mp3/mp3dec.o

ifeq ($(ASAN),1)
	CFLAGS += -fsanitize=address
	LFLAGS += -fsanitize=address
endif

gaplay: $(O) ../../o/debug/libgorilla.a
	$(CC) $(LFLAGS) -o gaplay $(O)

../../o/debug/libgorilla.a:
	make -C ../..

clean:
	rm -f $(O) gaplay

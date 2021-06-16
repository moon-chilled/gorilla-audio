CC ?= cc
CFLAGS = -I../../include -Os -g
LFLAGS = -Wl,-rpath=$(shell pwd)/../../o/debug -L../../o/debug -lgorilla
O := gaplay.o ../../contrib/mp3/mp3dec.o

ifeq ($(ASAN),1)
	CFLAGS += -fsanitize=address -fsanitize=undefined
	LFLAGS += -fsanitize=address -fsanitize=undefined
else ifeq ($(MSAN),1)
	CFLAGS += -fsanitize=memory -fsanitize=undefined
	LFLAGS += -fsanitize=memory -fsanitize=undefined
else ifeq ($(TSAN),1)
	CFLAGS += -fsanitize=thread -fsanitize=undefined
	LFLAGS += -fsanitize=thread -fsanitize=undefined
endif

gaplay: $(O) ../../o/debug/libgorilla.a
	$(CC) $(LFLAGS) -o gaplay $(O)

../../o/debug/libgorilla.a:
	make -C ../..

clean:
	rm -f $(O) gaplay

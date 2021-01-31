#######
#SETUP#
#######
TARGET :=
ifeq ($(OS),Windows_NT)
	TARGET := win32
else ifeq ($(shell uname),Darwin)
	TARGET := macos
else ifeq ($(shell uname),Linux)
	TARGET := linux
else ifeq ($(shell uname),FreeBSD)
	TARGET := freebsd
else ifeq ($(shell uname),OpenBSD)
	TARGET := openbsd
else
	_ := $(error unknown system, pls send patches)
endif

MODE ?= debug

CFLAGS := -Iext/libogg/include -Iext/libvorbis/include -Iext/libopus/include -Iext/opusfile/include
CFLAGS_release :=
CFLAGS_debug :=
CXXFLAGS :=
LFLAGS :=

CC ?= cc
CCLD ?= $(CC)
ifneq ($(CC),tcc)
	CFLAGS += -MMD
endif

ENABLE_XAUDIO2 := 0
ENABLE_PULSEAUDIO := 0
ENABLE_ALSA := 0
ENABLE_OSS := 0
ENABLE_OPENAL := 0

GA_SRC := src/ga/ga.c src/ga/memory.c src/ga/stream.c src/ga/system.c src/ga/devices/dummy.c src/ga/devices/wav.c
GAU_SRC := src/gau/gau.c src/gau/datasrc/file.c src/gau/datasrc/memory.c src/gau/samplesrc/loop.c src/gau/samplesrc/sound.c src/gau/samplesrc/stream.c src/gau/samplesrc/wav.c src/gau/samplesrc/ogg-vorbis.c src/gau/samplesrc/ogg-opus.c
OGG_SRC := ext/libogg/src/bitwise.c ext/libogg/src/framing.c
VORBIS_SRC := ext/libvorbis/lib/analysis.c ext/libvorbis/lib/bitrate.c ext/libvorbis/lib/block.c ext/libvorbis/lib/codebook.c ext/libvorbis/lib/envelope.c ext/libvorbis/lib/floor0.c ext/libvorbis/lib/floor1.c ext/libvorbis/lib/info.c ext/libvorbis/lib/lpc.c ext/libvorbis/lib/lsp.c ext/libvorbis/lib/mapping0.c ext/libvorbis/lib/mdct.c ext/libvorbis/lib/psy.c ext/libvorbis/lib/registry.c ext/libvorbis/lib/res0.c ext/libvorbis/lib/sharedbook.c ext/libvorbis/lib/smallft.c ext/libvorbis/lib/synthesis.c ext/libvorbis/lib/vorbisfile.c ext/libvorbis/lib/window.c
OPUSFILE_SRC := ext/opusfile/src/opusfile.c ext/opusfile/src/info.c ext/opusfile/src/internal.c ext/opusfile/src/stream.c 

GA_CFLAGS := -Iinclude

ifeq ($(TARGET),win32)
	CFLAGS += -DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS
	CFLAGS_debug += /Zi  #this is only cl; todo figure out clang/mingw
	CFLAGS += /MT /MTd   #should these be linker opts?
else
	CFLAGS += -Wall -Wextra -Wno-unused-parameter
	CFLAGS += -fPIC
	CFLAGS_debug += -Werror
	CFLAGS_debug += -g
	CFLAGS_release += -O2
endif

ifeq ($(TARGET),macos)
	CFLAGS += -mmacosx-version-min=10.5
	LFLAGS += -mmacosx-version-min=10.5
endif

ifeq ($(TARGET),win32)
	ENABLE_XAUDIO2 := 1
else ifeq ($(TARGET),macos)
	ENABLE_OPENAL := 1
else ifeq ($(TARGET),linux)
	ENABLE_PULSEAUDIO := 1
	ENABLE_ALSA := 1
else ifeq ($(TARGET),freebsd)
	ENABLE_OSS := 1
else ifeq ($(TARGET),openbsd)
	ENABLE_OPENAL := 1
endif

ifeq ($(ENABLE_XAUDIO2),1)
	# todo dx sdk (or, better yet, replace xaudio2 with wasapi)
	CFLAGS += -DENABLE_XAUDIO2
	CXXFLAGS += -DENABLE_XAUDIO2
	GA_SRC += src/ga/devices/xaudio2.cc
endif
ifeq ($(ENABLE_PULSEAUDIO),1)
	CFLAGS += -DENABLE_PULSEAUDIO `pkg-config --cflags libpulse-simple`
	LFLAGS += `pkg-config --libs libpulse-simple`
	GA_SRC += src/ga/devices/pulseaudio.c
endif
ifeq ($(ENABLE_ALSA),1)
	CFLAGS += -DENABLE_ALSA `pkg-config --cflags alsa`
	LFLAGS += `pkg-config --libs alsa`
	GA_SRC += src/ga/devices/alsa.c
endif
ifeq ($(ENABLE_OSS),1)
	CFLAGS += -DENABLE_OSS
	GA_SRC += src/ga/devices/oss.c
endif
ifeq ($(ENABLE_OPENAL),1)
	CFLAGS += -DENABLE_OPENAL `pkg-config --cflags openal`
	LFLAGS += `pkg-config --libs openal`
	GA_SRC += src/ga/devices/openal.c
endif


##########
#TEARDOWN#
##########

default: o/$(MODE)/libgorilla.so o/$(MODE)/libgorilla.a

include $(wildcard src/$(MODE)/ga/src/*.d src/$(MODE)/ga/*/*.d src/$(MODE)/gau/*.d src/$(MODE)/gau/*/*.d)

ext/libogg/include/ogg/config_types.h: ext/ogg_config_types.h
	cp ext/ogg_config_types.h ext/libogg/include/ogg/config_types.h
$(OGG_SRC): ext/libogg/include/ogg/config_types.h


o/$(MODE)/src/%.o: src/%.c
	@mkdir -p `dirname $@`
	$(CC) -c -o $@ $(CFLAGS) $(CFLAGS_$(MODE)) $(GA_CFLAGS) $<
o/$(MODE)/ext/%.o: ext/%.c
	@mkdir -p `dirname $@`
	$(CC) -c -o $@ $(CFLAGS) $(CFLAGS_$(MODE)) $<

OBJ := $(patsubst %.c,o/$(MODE)/%.o,$(GA_SRC) $(GAU_SRC) $(OGG_SRC) $(VORBIS_SRC) $(OPUSFILE_SRC))
o/$(MODE)/libgorilla.so: $(OBJ)
	@mkdir -p o/$(MODE)
	$(CCLD) -shared $(LFLAGS) -o o/$(MODE)/libgorilla.so $(OBJ)
o/$(MODE)/libgorilla.a: $(OBJ)
	@mkdir -p o/$(MODE)
	ar rcs o/$(MODE)/libgorilla.a $(OBJ)

clean:
	rm -rf o

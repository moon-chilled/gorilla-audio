#######
#SETUP#
#######
TARGET :=
LIBEXT := so
ifeq ($(OS),Windows_NT)
	TARGET := mingw
	LIBEXT := dll
else ifeq ($(shell uname),Darwin)
	TARGET := macos
	LIBEXT := dylib
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

CFLAGS := -Iext/libflac/include -Iext/libogg/include -Iext/libopus/include -Iext/opusfile/include -Iext/libvorbis/include -Iext/dr_libs -Iext/stb
CFLAGS_release :=
CFLAGS_debug :=
CXXFLAGS := $(CFLAGS)
LFLAGS :=

CC ?= cc
CXX ?= c++
CCLD ?= $(CC)
ifneq ($(CC),tcc)
	CFLAGS += -MMD
endif

BUILD_STATIC := 1
BUILD_DYNAMIC := 1

ENABLE_XAUDIO2 := 0
ENABLE_ARCAN := 0
ENABLE_SNDIO := 0
ENABLE_PULSEAUDIO := 0
ENABLE_ALSA := 0
ENABLE_OSS := 0
ENABLE_OPENAL := 0

ENABLE_OPUS := 1
ENABLE_VORBIS := 1
ENABLE_FLAC := 1

GA_SRC := src/ga/ga.c src/ga/trans.c src/ga/memory.c src/ga/stream.c src/ga/system.c src/ga/log.c src/ga/devices/dummy.c src/ga/devices/wav.c
GAU_SRC := src/gau/gau.c src/gau/datasrc/file.c src/gau/datasrc/memory.c src/gau/samplesrc/loop.c src/gau/samplesrc/sound.c src/gau/samplesrc/stream.c src/gau/samplesrc/wav.c src/gau/samplesrc/ogg-vorbis.c src/gau/samplesrc/ogg-opus.c src/gau/samplesrc/flac.c
OGG_SRC := ext/libogg/src/bitwise.c ext/libogg/src/framing.c
FLAC_SRC := ext/libflac/src/libFLAC/bitmath.c ext/libflac/src/libFLAC/bitreader.c ext/libflac/src/libFLAC/bitwriter.c ext/libflac/src/libFLAC/cpu.c ext/libflac/src/libFLAC/crc.c ext/libflac/src/libFLAC/fixed.c ext/libflac/src/libFLAC/fixed_intrin_sse2.c ext/libflac/src/libFLAC/fixed_intrin_ssse3.c ext/libflac/src/libFLAC/float.c ext/libflac/src/libFLAC/format.c ext/libflac/src/libFLAC/lpc.c ext/libflac/src/libFLAC/lpc_intrin_sse.c ext/libflac/src/libFLAC/lpc_intrin_sse2.c ext/libflac/src/libFLAC/lpc_intrin_sse41.c ext/libflac/src/libFLAC/lpc_intrin_avx2.c ext/libflac/src/libFLAC/md5.c ext/libflac/src/libFLAC/memory.c ext/libflac/src/libFLAC/metadata_iterators.c ext/libflac/src/libFLAC/metadata_object.c ext/libflac/src/libFLAC/stream_decoder.c ext/libflac/src/libFLAC/stream_encoder.c ext/libflac/src/libFLAC/stream_encoder_intrin_sse2.c ext/libflac/src/libFLAC/stream_encoder_intrin_ssse3.c ext/libflac/src/libFLAC/stream_encoder_intrin_avx2.c ext/libflac/src/libFLAC/stream_encoder_framing.c ext/libflac/src/libFLAC/window.c
ifeq ($(TARGET),mingw)
	FLAC_SRC += ext/libflac/src/libFLAC/windows_unicode_filenames.c
endif

OPUSFILE_SRC := ext/opusfile/src/opusfile.c ext/opusfile/src/info.c ext/opusfile/src/internal.c ext/opusfile/src/stream.c 
VORBIS_SRC := ext/libvorbis/lib/analysis.c ext/libvorbis/lib/bitrate.c ext/libvorbis/lib/block.c ext/libvorbis/lib/codebook.c ext/libvorbis/lib/envelope.c ext/libvorbis/lib/floor0.c ext/libvorbis/lib/floor1.c ext/libvorbis/lib/info.c ext/libvorbis/lib/lpc.c ext/libvorbis/lib/lsp.c ext/libvorbis/lib/mapping0.c ext/libvorbis/lib/mdct.c ext/libvorbis/lib/psy.c ext/libvorbis/lib/registry.c ext/libvorbis/lib/res0.c ext/libvorbis/lib/sharedbook.c ext/libvorbis/lib/smallft.c ext/libvorbis/lib/synthesis.c ext/libvorbis/lib/vorbisfile.c ext/libvorbis/lib/window.c

include ext/libopus/silk_sources.mk
include ext/libopus/celt_sources.mk
include ext/libopus/opus_sources.mk
OPUS_SRC := $(patsubst %,ext/libopus/%,$(SILK_SOURCES) $(SILK_SOURCES_FLOAT) $(CELT_SOURCES) $(OPUS_SOURCES) $(OPUS_SOURCES_FLOAT))

GA_CFLAGS := -Iinclude -DGAU_SUPPORT_FLAC=$(ENABLE_FLAC) -DGAU_SUPPORT_VORBIS=$(ENABLE_VORBIS) -DGAU_SUPPORT_OPUS=$(ENABLE_OPUS)
GA_CXXFLAGS := -Iinclude
FLAC_CFLAGS := -Iext/libflac/src/libFLAC/include -DHAVE_STDINT_H -DHAVE_LROUND -DFLAC__HAS_OGG=0 -DPACKAGE_VERSION="\"(gorilla audio)\""
OPUS_CFLAGS := -Iext/libopus/celt -Iext/libopus/silk -Iext/libopus/silk/float -DOPUS_BUILD -DUSE_ALLOCA -DHAVE_LRINT -DHAVE_LRINTF -DHAVE_STDINT_H -DPACKAGE_VERSION="\"(gorilla audio)\"" -DSKIP_CONFIG_H

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

ifeq ($(TARGET),mingw)
	CFLAGS += -DFLAC__NO_DLL
endif

# cl:
#CFLAGS += -DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS
#CFLAGS_debug += /Zi  #this is only cl; todo figure out clang/mingw
#CFLAGS += /MT /MTd   #should these be linker opts?

CFLAGS += -Wall -Wextra -Wno-unused-parameter
CFLAGS += -fwrapv -fno-delete-null-pointer-checks
CFLAGS += -fPIC
CFLAGS += -std=c99
#CFLAGS_debug += -Werror
CFLAGS_debug += -g
CFLAGS_release += -O2
LFLAGS += -lm
ifneq ($(TARGET),mingw)
	LFLAGS += -lpthread
endif

FLAC_CFLAGS += -DHAVE_FSEEKO

ifeq ($(TARGET),macos)
	CFLAGS += -mmacosx-version-min=10.5
	LFLAGS += -mmacosx-version-min=10.5
endif

ifeq ($(TARGET),mingw)
	ENABLE_XAUDIO2 := 1
else ifeq ($(TARGET),macos)
	ENABLE_OPENAL := 1
else ifeq ($(TARGET),linux)
	ENABLE_PULSEAUDIO := 1
	ENABLE_ALSA := 1
	CFLAGS += -DHAVE_ALLOCA_H
	FLAC_CFLAGS += -D_DEFAULT_SOURCE
else ifeq ($(TARGET),freebsd)
	ENABLE_OSS := 1
else ifeq ($(TARGET),openbsd)
	ENABLE_SNDIO := 1

	FLAC_CFLAGS += -D__BSD_VISIBLE -D__XPG_VISIBLE=420
endif

ifeq ($(ENABLE_XAUDIO2),1)
	# todo dx sdk (or, better yet, replace xaudio2 with wasapi)
	CFLAGS += -DENABLE_XAUDIO2
	CXXFLAGS += -DENABLE_XAUDIO2
	LFLAGS += -lxaudio2_8 -lole32
	GA_SRC += src/ga/devices/xaudio2.cc
endif
ifeq ($(ENABLE_ARCAN),1)
	CFLAGS += -DENABLE_ARCAN
	ifeq ($(ARCAN_LWA_BUILD),1)
		CFLAGS += $(ARCAN_LWA_CFLAGS)
	else
		CFLAGS += `pkg-config --cflags arcan-shmif`
		LFLAGS += `pkg-config --libs arcan-shmif`
	endif
	GA_SRC += src/ga/devices/arcan.c
endif
ifeq ($(ENABLE_SNDIO),1)
	CFLAGS += -DENABLE_SNDIO
	LFLAGS += -lsndio
	GA_SRC += src/ga/devices/sndio.c
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

ALL_SRC := $(GA_SRC) $(GAU_SRC)

ifeq ($(ENABLE_FLAC),1)
	ALL_SRC += $(FLAC_SRC)
endif

ifneq ($(ENABLE_OPUS)$(ENABLE_VORBIS),00)
	ALL_SRC += $(OGG_SRC)
endif
ifeq ($(ENABLE_OPUS),1)
	ALL_SRC += $(OPUS_SRC) $(OPUSFILE_SRC)
endif
ifeq ($(ENABLE_VORBIS),1)
	ALL_SRC += $(VORBIS_SRC)
endif


##########
#TEARDOWN#
##########

ifeq ($(BUILD_STATIC),1)
default: o/$(MODE)/libgorilla.a
endif

ifeq ($(BUILD_DYNAMIC),1)
default: o/$(MODE)/libgorilla.$(LIBEXT)
endif

include $(wildcard o/$(MODE)/src/ga/src/*.d o/$(MODE)/src/ga/*/*.d o/$(MODE)/src/gau/*.d o/$(MODE)/src/gau/*/*.d)

ext/libogg/include/ogg/config_types.h: ext/ogg_config_types.h
	cp ext/ogg_config_types.h ext/libogg/include/ogg/config_types.h
$(OGG_SRC): ext/libogg/include/ogg/config_types.h


o/$(MODE)/src/%.o: src/%.c
	@mkdir -p `dirname $@`
	$(CC) -c -o $@ $(CFLAGS) $(CFLAGS_$(MODE)) $(GA_CFLAGS) $<
o/$(MODE)/src/%.o: src/%.cc
	@mkdir -p `dirname $@`
	$(CXX) -c -o $@ $(CXXFLAGS) $(CXXFLAGS_$(MODE)) $(GA_CXXFLAGS) $<
o/$(MODE)/ext/%.o: ext/%.c
	@mkdir -p `dirname $@`
	$(CC) -c -o $@ $(CFLAGS) $(CFLAGS_$(MODE)) $<
o/$(MODE)/ext/libopus/%.o: ext/libopus/%.c
	@mkdir -p `dirname $@`
	$(CC) -c -o $@ $(CFLAGS) $(CFLAGS_$(MODE)) $(OPUS_CFLAGS) $<
o/$(MODE)/ext/libflac/%.o: ext/libflac/%.c
	@mkdir -p `dirname $@`
	$(CC) -c -o $@ $(CFLAGS) $(CFLAGS_$(MODE)) $(FLAC_CFLAGS) $<

OBJ := $(patsubst %.c,o/$(MODE)/%.o,$(ALL_SRC))
OBJ := $(patsubst %.cc,o/$(MODE)/%.o,$(OBJ))
o/$(MODE)/libgorilla.$(LIBEXT): $(OBJ)
	@mkdir -p o/$(MODE)
	$(CCLD) -shared -o o/$(MODE)/libgorilla.$(LIBEXT) $(OBJ) $(LFLAGS)
o/$(MODE)/libgorilla.a: $(OBJ)
	@mkdir -p o/$(MODE)
	ar rcs o/$(MODE)/libgorilla.a $(OBJ)

clean:
	rm -rf o/*/src
spotless:
	rm -rf o

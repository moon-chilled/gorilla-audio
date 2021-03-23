/** Gorilla Internal Interface.
 *
 *  Internal data structures and functions.
 *
 *  \file ga_internal.h
 */

/** \defgroup internal Internal Interface
 * Internal data structures and functions used by Gorilla Audio.
 */

#ifndef _GORILLA_GA_INTERNAL_H
#define _GORILLA_GA_INTERNAL_H

#include "gorilla/ga.h"
#include "gorilla/ga_system.h"
#include "gorilla/ga_u_internal.h"

/************/
/*  Device  */
/************/
/** Internal device object definition.
 *
 *  \ingroup internal
 *  \defgroup intDevice Device
 */

/** Procedure pointers for interacting with a given device
 *  \warning don't call into these directly.  Call the wrappers (ga_device_*)
 *           instead
 */
typedef struct {
	ga_result (*open)(GaDevice *dev);
	ga_result (*check)(GaDevice *dev, u32 *num_buffers); //if 'push'
	void *(*get_buffer)(GaDevice *dev); //optional
	ga_result (*queue)(GaDevice *dev, void *buffer); //iff 'push'
	ga_result (*register_queuer)(GaDevice *dev, GaCbDeviceQueuer queuer, void *ctx); //iff 'pull'
	ga_result (*close)(GaDevice *device);
} GaXDeviceProcs;

extern GaXDeviceProcs gaX_deviceprocs_dummy;
extern GaXDeviceProcs gaX_deviceprocs_WAV;

#ifdef ENABLE_OSS
extern GaXDeviceProcs gaX_deviceprocs_OSS;
#endif

#ifdef ENABLE_XAUDIO2
extern GaXDeviceProcs gaX_deviceprocs_XAudio2;
#endif

#ifdef ENABLE_ARCAN
extern GaXDeviceProcs gaX_deviceprocs_Arcan;
#endif

#ifdef ENABLE_SNDIO
extern GaXDeviceProcs gaX_deviceprocs_sndio;
#endif

#ifdef ENABLE_PULSEAUDIO
extern GaXDeviceProcs gaX_deviceprocs_PulseAudio;
#endif

#ifdef ENABLE_ALSA
extern GaXDeviceProcs gaX_deviceprocs_ALSA;
#endif

#ifdef ENABLE_OPENAL
extern GaXDeviceProcs gaX_deviceprocs_OpenAL;
#endif

/** Device-specific data structure
 *  Redefined separately in the translation unit for each device
 */
typedef struct GaXDeviceImpl GaXDeviceImpl;


/** Hardware device abstract data structure [\ref SINGLE_CLIENT].
 *
 *  Abstracts the platform-specific details of presenting audio buffers to sound playback hardware.
 *
 *  \ingroup intDevice
 *  \warning Never instantiate a GaDevice directly, unless you are implementing a new concrete
 *           device implementation. Instead, you should use ga_device_open().
 */
struct GaDevice {
	GaXDeviceImpl *impl;

	// use iff get_buffer is missing to avoid spurious allocations
	void *buffer;

	GaXDeviceProcs procs;

	GaDeviceType type;
	u32 num_buffers;
	u32 num_frames;
	GaFormat format;
	GaDeviceClass class;

	//GaXDeviceImpl impl[]; //stupid wg14
};

/*****************/
/*  Data Source  */
/*****************/
/** Internal data source object definition.
 *
 *  \ingroup internal
 *  \defgroup intDataSource Data Source
 */

/** Abstract data source data structure [\ref MULTI_CLIENT].
 *
 *  A data source is a source of binary data, such as a file or socket, that
 *  generates bytes of binary data. This data is usually piped through a sample
 *  source to generate actual PCM audio data.
 *
 *  \ingroup intDataSource
 */
struct GaDataSource {
	GaCbDataSource_Read read;     /**< Internal read callback. */
	GaCbDataSource_Seek seek;     /**< Internal seek callback (optional). */
	GaCbDataSource_Tell tell;     /**< Internal tell callback. */
	GaCbDataSource_Eof eof;       /**< Internal eof callback. */
	GaCbDataSource_Close close;   /**< Internal close callback (optional). */
	GaDataSourceContext *context; /**< opaque context for callbacks. */
	GaDataAccessFlags flags;      /**< Flags defining which functionality this data source supports (see [\ref globDefs]). */
	RC refCount;                  /**< Reference count. */
};

/*******************/
/*  Sample Source  */
/*******************/
struct GaSampleSource {
	GaCbSampleSource_Read read;
	GaCbSampleSource_End end;
	GaCbSampleSource_Ready ready; // OPTIONAL
	GaCbSampleSource_Seek seek;   // OPTIONAL
	GaCbSampleSource_Tell tell;   // OPTIONAL
	GaCbSampleSource_Close close; // OPTIONAL
	GaSampleSourceContext *context;
	GaFormat format;
	GaDataAccessFlags flags;
	RC refCount;
};

/************/
/*  Memory  */
/************/
struct GaMemory {
	void *data;
	usz size;
	RC refCount;
};

/***********/
/*  Sound  */
/***********/
struct GaSound {
	GaMemory *memory;
	GaFormat format;
	usz num_frames;
	RC refCount;
};

/************/
/*  Handle  */
/************/
typedef enum {
	GaHandleState_Unknown,
	GaHandleState_Initial,
	GaHandleState_Playing,
	GaHandleState_Stopped,
	GaHandleState_Finished,
	GaHandleState_Destroyed,
} GaHandleState;

typedef enum {
	GaXHandleParam_Pitch,
	GaXHandleParam_Pan,
	GaXHandleParam_Gain,
	GaXHandleParam_LastPan,
	GaXHandleParam_LastGain,
	GaXHandleParam_Max,
} GaXHandleParam;

typedef struct {
	f32 pitch;
	f32 gain, last_gain;
	f32 pan, last_pan;
} JukeboxState;

struct GaHandle {
	GaMixer *mixer;
	GaResamplingState *resample_state; //non-null iff format.sample_rate != mixerformat.sample_rate
	GaCbHandleFinish callback;
	void *context;
	GaHandleState state;
	JukeboxState jukebox; //point either to def_jukebox, or group->jukebox
	u32 jukebox_stamps[GaXHandleParam_Max];
	GaLink dispatch_link;
	GaLink mix_link;
	GaMutex mutex;
	GaSampleSource *sample_src;

	GaHandleGroup *group;
	GaLink group_link;

	//JukeboxState def_jukebox;
};

struct GaHandleGroup {
	GaMixer *mixer;

	GaLink handles;

	GaMutex mutex;

	JukeboxState jukebox;
	u32 jukebox_stamps[GaXHandleParam_Max];
};

/************/
/*  Mixer  */
/************/
struct GaMixer {
	GaFormat format;
	GaFormat mix_format;
	u32 num_frames;
	s32 *mix_buffer;
	GaLink dispatch_list;
	GaMutex dispatch_mutex;
	GaLink mix_list;
	GaMutex mix_mutex;
	GaHandleGroup handle_group;
	atomic_bool suspended;
};


struct GaStreamManager {
	GaLink stream_list;
	GaMutex mutex;
};

struct GaBufferedStream {
	GaLink *stream_link;
	GaSampleSource *inner_src;
	GaCircBuffer *buffer;
	GaMutex produce_mutex;
	GaMutex seek_mutex;
	GaMutex read_mutex;
	RC refCount;
	GaLink tell_jumps;
	GaFormat format;
	usz buffer_size;
	ssz seek;
	usz tell;
	usz next_frame;
	bool end;
	GaDataAccessFlags flags;
};

#define ga_list_iterate(T, obj, head) \
	for (GaLink *_dummy = (GaLink*)1, *_next, *_head = (head), *link = _head->next; _dummy; _dummy = NULL) /*conditional constant propagation is coming for you!*/ \
	for (T *obj; (_next = link->next), (obj = (T*)link->data), (link != _head); link = _next)

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
# define ENDIAN(little, big) little
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# define ENDIAN(little, big) big
#else
# error Unable to determine endianness
#endif

static inline u16 ga_endian_bswap2(u16 x) {
	return (x<<8) | (x>>8);
}
static inline u32 ga_endian_bswap4(u32 x) {
	return ga_endian_bswap2(x >> 16)
	     | ga_endian_bswap2(x) << 16;
}

static inline u16 ga_endian_tobe2(u16 x) {
	return ENDIAN(ga_endian_bswap2(x), x);
}
static inline u32 ga_endian_tobe4(u32 x) {
	return ENDIAN(ga_endian_bswap4(x), x);
}
static inline u16 ga_endian_tole2(u16 x) {
	return ENDIAN(x, ga_endian_bswap2(x));
}
static inline u32 ga_endian_tole4(u32 x) {
	return ENDIAN(x, ga_endian_bswap4(x));
}

static inline u16 ga_endian_frombe2(u16 x) {
	return ga_endian_tobe2(x);
}
static inline u32 ga_endian_frombe4(u32 x) {
	return ga_endian_tobe4(x);
}
static inline u16 ga_endian_fromle2(u16 x) {
	return ga_endian_tole2(x);
}
static inline u32 ga_endian_fromle4(u32 x) {
	return ga_endian_tole4(x);
}

static inline s32 ga_add32_saturate(s32 x, s32 y) {
	return clamp((s64)x + (s64)y, GA_S32_MIN, GA_S32_MAX);
}

#endif /* _GORILLA_GA_INTERNAL_H */

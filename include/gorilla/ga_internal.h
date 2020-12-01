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

#include <stdatomic.h>
#include <assert.h>

#include "gorilla/common/gc_common.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef _Atomic gc_int32 gc_atomic_int32;
typedef _Atomic gc_uint32 gc_atomic_uint32;
typedef _Atomic gc_size gc_atomic_size;

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
	gc_result (*open)(ga_Device *dev);
	gc_int32 (*check)(ga_Device *dev);
	gc_result (*queue)(ga_Device *dev, void *buffer);
	gc_result (*close)(ga_Device *device);
} gaX_DeviceProcs;

extern gaX_DeviceProcs gaX_deviceprocs_dummy;

#ifdef ENABLE_OSS
extern gaX_DeviceProcs gaX_deviceprocs_OSS;
#endif

#ifdef ENABLE_XAUDIO2
extern gaX_DeviceProcs gaX_deviceprocs_XAudio2;
#endif

#ifdef ENABLE_PULSEAUDIO
extern gaX_DeviceProcs gaX_deviceprocs_PulseAudio;
#endif

#ifdef ENABLE_ALSA
extern gaX_DeviceProcs gaX_deviceprocs_ALSA;
#endif

#ifdef ENABLE_OPENAL
extern gaX_DeviceProcs gaX_deviceprocs_OpenAL;
#endif

/** Device-specific data structure
 *  Redefined separately in the translation unit for each device
 */
typedef struct gaX_DeviceImpl gaX_DeviceImpl;


/** Hardware device abstract data structure [\ref SINGLE_CLIENT].
 *
 *  Abstracts the platform-specific details of presenting audio buffers to sound playback hardware.
 *
 *  \ingroup intDevice
 *  \warning You can only have one device open at a time.
 *  \warning Never instantiate a ga_Device directly, unless you are implementing a new concrete
 *           device implementation. Instead, you should use ga_device_open().
 */
struct ga_Device {
	GaDeviceType dev_type;
	gc_uint32 num_buffers;
	gc_uint32 num_samples;
	ga_Format format;

	gaX_DeviceProcs procs;
	gaX_DeviceImpl *impl;
};

/*****************/
/*  Data Source  */
/*****************/
/** Internal data source object definition.
 *
 *  \ingroup internal
 *  \defgroup intDataSource Data Source
 */

/** Data source read callback prototype.
 *
 *  \ingroup intDataSource
 *  \param context User context (pointer to the first byte after the data source).
 *  \param dst Destination buffer into which bytes should be read. Must
 *                be at least (size * count) bytes in size.
 *  \param size Size of a single element (in bytes).
 *  \param count Number of elements to read.
 *  \return Total number of bytes read into the destination buffer.
 */
typedef gc_size (*GaCbDataSource_Read)(void *context, void *dst, gc_size size, gc_size count);

/** Data source seek callback prototype.
 *
 *  \ingroup intDataSource
 *  \param context User context (pointer to the first byte after the data source).
 *  \param offset Offset (in bytes) from the specified seek origin.
 *  \param origin Seek origin (see [\ref globDefs]).
 *  \return If seek succeeds, the callback should return GC_SUCCESS, otherwise it should return GC_ERROR_GENERIC.
 *  \warning Data sources with GA_FLAG_SEEKABLE should always provide a seek callback.
 *  \warning Data sources with GA_FLAG_SEEKABLE set should only return -1 in the case of
 *           an invalid seek request.
 *  \todo Define a less-confusing contract for extending/defining this function.
 */
typedef gc_result (*GaCbDataSource_Seek)(void *context, gc_ssize offset, GaSeekOrigin whence);

/** Data source tell callback prototype.
 *
 *  \ingroup intDataSource
 *  \param context User context (pointer to the first byte after the data source).
 *  \return The current data source read position.
 */
typedef gc_size (*GaCbDataSource_Tell)(void *context);

/** Data source close callback prototype.
 *
 *  \ingroup intDataSource
 *  \param context User context (pointer to the first byte after the data source).
 */
typedef void (*GaCbDataSource_Close)(void *context);

/** Abstract data source data structure [\ref MULTI_CLIENT].
 *
 *  A data source is a source of binary data, such as a file or socket, that
 *  generates bytes of binary data. This data is usually piped through a sample
 *  source to generate actual PCM audio data.
 *
 *  \ingroup intDataSource
 *  \todo Design a clearer/better system for easily extending this data type.
 */
struct ga_DataSource {
	GaCbDataSource_Read read; /**< Internal read callback. */
	GaCbDataSource_Seek seek; /**< Internal seek callback (optional). */
	GaCbDataSource_Tell tell; /**< Internal tell callback (optional). */
	GaCbDataSource_Close close; /**< Internal close callback (optional). */
	gc_atomic_uint32 refCount; /**< Reference count. */
	GaDataAccessFlags flags; /**< Flags defining which functionality this data source supports (see [\ref globDefs]). */
};

/** Initializes the reference count and other default values.
 *
 *  Because ga_DataSource is an abstract data type, this function should not be
 *  called except when implement a concrete data source implementation.
 *
 *  \ingroup intDataSource
 */
void ga_data_source_init(ga_DataSource *data_src);

/*******************/
/*  Sample Source  */
/*******************/
typedef gc_size (*GaCbSampleSource_Read)(void *context, void *dst, gc_size num_samples,
                                           GaCbOnSeek onseek, void *seek_ctx);
typedef gc_bool (*GaCbSampleSource_End)(void *context);
typedef gc_bool (*GaCbSampleSource_Ready)(void *context, gc_size num_samples);
typedef gc_result (*GaCbSampleSource_Seek)(void *context, gc_size sample_offset);
typedef gc_result (*GaCbSampleSource_Tell)(void *context, gc_size *samples, gc_size *total_samples);
typedef void (*GaCbSampleSource_Close)(void *context);

struct ga_SampleSource {
	GaCbSampleSource_Read read;
	GaCbSampleSource_End end;
	GaCbSampleSource_Ready ready;
	GaCbSampleSource_Seek seek; /* OPTIONAL */
	GaCbSampleSource_Tell tell; /* OPTIONAL */
	GaCbSampleSource_Close close; /* OPTIONAL */
	ga_Format format;
	gc_atomic_uint32 refCount;
	GaDataAccessFlags flags;
};

void ga_sample_source_init(ga_SampleSource *sample_src);

/************/
/*  Memory  */
/************/
struct ga_Memory {
	void *data;
	gc_size size;
	gc_atomic_uint32 refCount;
};

/***********/
/*  Sound  */
/***********/
struct ga_Sound {
	ga_Memory* memory;
	ga_Format format;
	gc_size num_samples;
	gc_atomic_uint32 refCount;
};

/************/
/*  Handle  */
/************/
typedef enum {
	GA_HANDLE_STATE_UNKNOWN,
	GA_HANDLE_STATE_INITIAL,
	GA_HANDLE_STATE_PLAYING,
	GA_HANDLE_STATE_STOPPED,
	GA_HANDLE_STATE_FINISHED,
	GA_HANDLE_STATE_DESTROYED,
} GA_HANDLE_STATE;

struct ga_Handle {
	ga_Mixer* mixer;
	ga_FinishCallback callback;
	void* context;
	GA_HANDLE_STATE state;
	gc_float32 gain;
	gc_float32 pitch;
	gc_float32 pan;
	gc_Link dispatch_link;
	gc_Link mix_link;
	gc_Mutex *mutex;
	ga_SampleSource *sample_src;
	volatile gc_int32 finished;
};

/************/
/*  Mixer  */
/************/
struct ga_Mixer {
	ga_Format format;
	ga_Format mix_format;
	gc_size num_samples;
	gc_int32 *mix_buffer;
	gc_Link dispatch_list;
	gc_Mutex *dispatch_mutex;
	gc_Link mix_list;
	gc_Mutex *mix_mutex;
};


struct ga_StreamManager {
	gc_Link stream_list;
	gc_Mutex *mutex;
};

struct ga_BufferedStream {
	gc_Link *stream_link;
	ga_SampleSource *inner_src;
	gc_CircBuffer *buffer;
	gc_Mutex *produce_mutex;
	gc_Mutex *seek_mutex;
	gc_Mutex *read_mutex;
	gc_atomic_uint32 refCount;
	gc_Link tell_jumps;
	ga_Format format;
	gc_size buffer_size;
	gc_int32 seek;
	gc_int32 tell;
	gc_int32 next_sample;
	gc_int32 end;
	gc_int32 flags;
};

static inline gc_bool gcX_decref(gc_atomic_uint32 *count) {
	gc_atomic_uint32 old = atomic_fetch_sub(count, 1);
	assert(old);
	return old == 1;
}

#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _GORILLA_GA_INTERNAL_H */

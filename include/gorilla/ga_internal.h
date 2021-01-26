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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

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
	s32 (*check)(GaDevice *dev);
	ga_result (*queue)(GaDevice *dev, void *buffer);
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
 *  \warning You can only have one device open at a time.
 *  \warning Never instantiate a GaDevice directly, unless you are implementing a new concrete
 *           device implementation. Instead, you should use ga_device_open().
 */
struct GaDevice {
	GaDeviceType dev_type;
	u32 num_buffers;
	u32 num_samples;
	GaFormat format;

	GaXDeviceProcs procs;
	GaXDeviceImpl *impl;
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
typedef usz (*GaCbDataSource_Read)(void *context, void *dst, usz size, usz count);

/** Data source seek callback prototype.
 *
 *  \ingroup intDataSource
 *  \param context User context (pointer to the first byte after the data source).
 *  \param offset Offset (in bytes) from the specified seek origin.
 *  \param origin Seek origin (see [\ref globDefs]).
 *  \return If seek succeeds, the callback should return GA_OK, otherwise it should return GA_ERR_GENERIC.
 *  \warning Data sources with GaDataAccessFlag_Seekable should always provide a seek callback.
 *  \warning Data sources with GaDataAccessFlag_Seekable set should only return an error in the case of
 *           an invalid seek request.
 *  \todo Define a less-confusing contract for extending/defining this function.
 */
typedef ga_result (*GaCbDataSource_Seek)(void *context, ssz offset, GaSeekOrigin whence);

/** Data source tell callback prototype.
 *
 *  \ingroup intDataSource
 *  \param context User context (pointer to the first byte after the data source).
 *  \return The current data source read position.
 */
typedef usz (*GaCbDataSource_Tell)(void *context);

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
struct GaDataSource {
	GaCbDataSource_Read read;   /**< Internal read callback. */
	GaCbDataSource_Seek seek;   /**< Internal seek callback (optional). */
	GaCbDataSource_Tell tell;   /**< Internal tell callback (optional). */
	GaCbDataSource_Close close; /**< Internal close callback (optional). */
	RC refCount;                /**< Reference count. */
	GaDataAccessFlags flags;    /**< Flags defining which functionality this data source supports (see [\ref globDefs]). */
};

/** Initializes the reference count and other default values.
 *
 *  Because GaDataSource is an abstract data type, this function should not be
 *  called except when implement a concrete data source implementation.
 *
 *  \ingroup intDataSource
 */
void ga_data_source_init(GaDataSource *data_src);

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
	usz num_samples;
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

struct GaHandle {
	GaMixer *mixer;
	GaCbHandleFinish callback;
	void *context;
	GaHandleState state;
	f32 gain;
	f32 pitch;
	f32 pan;
	GaLink dispatch_link;
	GaLink mix_link;
	GaMutex mutex;
	GaSampleSource *sample_src;
};

/************/
/*  Mixer  */
/************/
struct GaMixer {
	GaFormat format;
	GaFormat mix_format;
	u32 num_samples;
	s32 *mix_buffer;
	GaLink dispatch_list;
	GaMutex dispatch_mutex;
	GaLink mix_list;
	GaMutex mix_mutex;
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
	s32 seek;
	s32 tell;
	s32 next_sample;
	s32 end;
	GaDataAccessFlags flags;
};

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _GORILLA_GA_INTERNAL_H */

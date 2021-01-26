/** Gorilla Audio API.
 *
 *  Data structures and functions for realtime audio playback.
 *
 *  \file ga.h
 */

/** \mainpage
 *  \htmlinclude manual.htm
 */

#ifndef GORILLA_GA_H
#define GORILLA_GA_H

#include "gorilla/ga_types.h"
#include "gorilla/ga_system.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Data structures and functions.
 *
 *  \defgroup external Audio API (GA)
 */

/*************/
/*  Version  */
/*************/
/** API version and related functions.
 *
 *  \defgroup version Version
 *  \ingroup external
 */

/** Major version number.
 *
 *  Major version changes indicate a massive rewrite or refactor, and/or changes
 *  where API backwards-compatibility is greatly compromised. \ingroup version
 *
 *  \ingroup version
 */
#define GA_VERSION_MAJOR 0

/** Minor version number.
 *
 *  Minor version changes indicate development milestones, large changes, and/or
 *  changes where API backwards-compatibility is somewhat compromised.
 *
 *  \ingroup version
 */
#define GA_VERSION_MINOR 3

/** Revision version number.
 *
 *  Revision version changes indicate a new version pushed to the trunk, and/or
 *  changes where API backwards-compatibility is trivially compromised.
 *
 *  \ingroup version
 */
#define GA_VERSION_REV 3

/** Compares the API version against the specified version.
 *
 *  \ingroup version
 *  \param major Major version to compare against
 *  \param minor Minor version to compare against
 *  \param rev Revision version to compare against
 *  \return true iff the specified version is compatible with the API version
 *
 *  For example: `assert(ga_version_compatible(GA_VERSION_MAJOR, GA_VERSION_MINOR, GA_VERSION_REV)`
 */
ga_bool ga_version_check(ga_sint32 major, ga_sint32 minor, ga_sint32 rev);


/************************/
/*  Global Definitions  */
/************************/
/** Global flags and definitions.
 *
 * \ingroup external
 * \defgroup globDefs Global Definitions
 */

/** Flags that define properties of data and sample sources.
 *
 *  \ingroup globDefs
 */
typedef enum {
	GaDataAccessFlag_Seekable   = 0x1,  /**< Does the data source support seeking?  (E.G. true for files, but not for network streams.) */
	GaDataAccessFlag_Threadsafe = 0x2,  /**< Does the source allow for concurrent access? */
} GaDataAccessFlags;


/***********************/
/*  Memory Management  */
/***********************/
/** Memory management policies for different types of objects.
 *
 *  \ingroup external
 *  \defgroup memManagement Memory Management
 */
#define POD /**< The object is POD (plain-ol'-data), and can be allocated/copied freely.
                 \ingroup memManagement */
#define SINGLE_CLIENT /**< The object has a single client (owner). The object should be
                           created/opened by its client, and then destroyed/closed when the
                           client is done with it. The object itself should never be copied.
                           Instead, a pointer to the object should be copied. The client
                           must never use the object after destroying it. \ingroup memManagement */
#define MULTI_CLIENT /**< The object has multiple clients (owners), and is reference-counted.
                          The object should be created by its first client. Additional
                          clients should call *_acquire() to add new references. Whenever a
                          client is done using the object, it should call *_release() to
                          remove its reference. When the last reference is removed, the
                          object will be freed. A client must never use the object after
                          releasing its reference. The object itself should never be copied.
                          Instead, a pointer to the object should be copied. \ingroup memManagement*/


/************/
/*  Format  */
/************/
/** Audio format definition data structure and associated functions.
 *
 *  \ingroup external
 *  \defgroup GaFormat Format
 */

/** Audio format data structure [\ref POD].
 *
 *  Stores the format (sample rate, bps, channels) for PCM audio data.
 *
 *  This object may be used on any thread.
 *
 *  \ingroup GaFormat
 */
typedef struct {
	ga_uint32 sample_rate; /**< Sample rate (usually 44100) */
	ga_uint32 bits_per_sample; /**< Bits per PCM sample (usually 16) */
	ga_uint32 num_channels; /**< Number of audio channels (1 for mono, 2 for stereo) */
} GaFormat;

/** Retrieves the sample size (in bytes) of a specified format.
 *
 *  \ingroup GaFormat
 *  \param format Format of the PCM data
 *  \return Sample size (in bytes) of the specified format
 */
ga_uint32 ga_format_sample_size(GaFormat *format);

/** Converts a discrete number of PCM samples into the duration (in seconds) it
 *  will take to play back.
 *
 *  \ingroup GaFormat
 *  \param format Format of PCM sample data
 *  \param samples Number of PCM samples
 *  \return Duration (in seconds) it will take to play back
 */
ga_float32 ga_format_to_seconds(GaFormat *format, ga_usize samples);

/** Converts a duration (in seconds) into the discrete number of PCM samples it
 *  will take to play for that long.
 *
 *  \ingroup GaFormat
 *  \param format Format of PCM sample data
 *  \param seconds Duration (in seconds)
 *  \return Number of PCM samples it will take to play back for the given time
 */
ga_sint32 ga_format_to_samples(GaFormat *format, ga_float32 seconds);


/************/
/*  Device  */
/************/
/** Abstract device data structure and associated functions.
 *
 *  \ingroup external
 *  \defgroup GaDevice Device
 */
typedef enum {
	GaDeviceType_Default    = -1, /**< Default device type (based on hard-coded priorities) */
	GaDeviceType_Unknown,         /**< Unknown (invalid) device type */
	GaDeviceType_Dummy,           /**< Dummy device, doesn't actually play anything */
	GaDeviceType_WAV,             /**< Write audio to WAV file */
	GaDeviceType_OSS,             /**< OSS playback device (mainly FreeBSD) */
	GaDeviceType_XAudio2,         /**< XAudio2 playback device (Windows-only) */
	GaDeviceType_PulseAudio,      /**< PulseAudio playback device (cross-platform, mainly for linux) */
	GaDeviceType_ALSA,            /**< ALSA playback device (mainly for linux) */
	GaDeviceType_OpenAL,          /**< OpenAL playback device (cross-platform) */
} GaDeviceType;

/** Hardware device abstract data structure [\ref SINGLE_CLIENT].
 *
 *  Abstracts the platform-specific details of presenting audio buffers to sound playback hardware.
 *
 *  If a new buffer is not queued prior to the last buffer finishing playback,
 *  the device has 'starved', and audible stuttering may occur.  This can be
 *  resolved by creating longer and/or more buffers when opening the device.
 *  The number of available presentation buffers is platform-specific, and is
 *  only guaranteed to be >= 2.  To find out how many buffers a device has,
 *  you can query it using ga_device_check() immediately after creating the
 *  device.
 *
 *  This object may only be used on the main thread.
 *
 *  \ingroup GaDevice
 *  \warning You can only have one device open at-a-time.
 *  \todo Create a way to query the actual buffers/samples/format of the opened device.
 *        ga_device_check() does not work in all use cases (such as GauManager))
 */
typedef struct GaDevice GaDevice;

/** Opens a concrete audio device.
 *
 *  \ingroup GaDevice
 *  \param type Requested and received device type (former is usually GaDeviceType_Default).
 *  \param num_buffers Requested and received number of buffers.
 *  \param num_samples Requested and received sample buffer size.
 *  \param format Requested device output format (usually 16-bit/44100/stereo).
 *  \return Concrete instance of the requested device type.  NULL if a suitable device could not be opened.
 *  \warning num_buffers, num_samples, and format are /requests/ to the audio
 *           device and may not necessarily be fulfilled.  The actually
 *           received values (as well as type, when it is GaDeviceType_Default)
 *           will be written back out to the same
 *           locations.  If you pass in NULL for any of these arguments, a
 *           reasonable default will be chosen.
 */
GaDevice *ga_device_open(GaDeviceType *type,
                          ga_uint32 *num_buffers,
                          ga_uint32 *num_samples,
                          GaFormat *format);

/** Checks the number of free (unqueued) buffers.
 *
 *  \ingroup GaDevice
 *  \param device Device to check.
 *  \return Number of free (unqueued) buffers.
 */
ga_sint32 ga_device_check(GaDevice *device);

/** Adds a buffer to a device's presentation queue.
 *
 *  \ingroup GaDevice
 *  \param device Device in which to queue the buffer.
 *  \param buffer Buffer to add to the presentation queue.
 *  \return GA_OK if the buffer was queued successfully. GA_ERR_GENERIC if not.
 *  \warning You should always call ga_device_check() prior to queueing a buffer! If
 *           there isn't a free (unqueued) buffer, the operation will fail.
 */
ga_result ga_device_queue(GaDevice *device, void *buffer);

/** Closes an open audio device.
 *
 *  \ingroup GaDevice
 *  \param device Device to close.
 *  \return GA_OK if the device was closed successfully. GA_ERR_GENERIC if not.
 *  \warning The client must never use a device after calling ga_device_close().
 */
ga_result ga_device_close(GaDevice *device);


/*****************/
/*  Data Source  */
/*****************/
/** Abstract data source data structure and associated functions.
 *
 *  \ingroup external
 *  \defgroup GaDataSource Data Source
 *  \todo Write an tutorial on how to write a GaDataSource concrete implementation.
 *  \todo Design a clearer/better system for easily extending this data type.
 */

/** Abstract data source data structure [\ref MULTI_CLIENT].
 *
 *  A data source is a source of binary data, such as a file or socket, that
 *  generates bytes of binary data. This data is usually piped through a sample
 *  source to generate actual PCM audio data.
 *
 *  This object may only be used on the main thread.
 *
 *  \ingroup GaDataSource
 */
typedef struct GaDataSource GaDataSource;

/** Enumerated seek origin values.
*
*  Used when seeking within data sources.
*
*  \ingroup GaDataSource
*  \defgroup seekOrigins Seek Origins
*/
typedef enum {
	GaSeekOrigin_Set, /**< Seek to an offset from the start of the source. \ingroup seekOrigins */
	GaSeekOrigin_Cur, /**< Seek to an offset from the current seek position. \ingroup seekOrigins */
	GaSeekOrigin_End, /**< Seek to an offset from the end of the source. \ingroup seekOrigins */
} GaSeekOrigin;

/** Abstract context for data source callbacks */
typedef struct GaDataSourceContext GaDataSourceContext;

/** Data source read callback prototype.
 *
 *  \ingroup intDataSource
 *  \param context User context.
 *  \param dst Destination buffer into which bytes should be read. Must
 *                be at least (size * count) bytes in size.
 *  \param size Size of a single element (in bytes).
 *  \param count Number of elements to read.
 *  \return Total number of bytes read into the destination buffer.
 */
typedef ga_usize (*GaCbDataSource_Read)(GaDataSourceContext *context, void *dst, ga_usize size, ga_usize count);

/** Data source seek callback prototype.
 *
 *  \ingroup intDataSource
 *  \param context User context.
 *  \param offset Offset (in bytes) from the specified seek origin.
 *  \param origin Seek origin (see [\ref globDefs]).
 *  \return If seek succeeds, the callback should return GA_OK, otherwise it should return GA_ERR_GENERIC.
 *  \warning Data sources with GaDataAccessFlag_Seekable should always provide a seek callback.
 *  \warning Data sources with GaDataAccessFlag_Seekable set should only return an error in the case of
 *           an invalid seek request.
 *  \todo Define a less-confusing contract for extending/defining this function.
 */
typedef ga_result (*GaCbDataSource_Seek)(GaDataSourceContext *context, ga_ssize offset, GaSeekOrigin whence);

/** Data source tell callback prototype.
 *
 *  \ingroup intDataSource
 *  \param context User context.
 *  \return The current data source read position.
 */
typedef ga_usize (*GaCbDataSource_Tell)(GaDataSourceContext *context);

/** Data source close callback prototype.
 *
 *  \ingroup intDataSource
 *  \param context User context.
 */
typedef void (*GaCbDataSource_Close)(GaDataSourceContext *context);

typedef struct {
	GaCbDataSource_Read read;
	GaCbDataSource_Seek seek;   // OPTIONAL
	GaCbDataSource_Tell tell;
	GaCbDataSource_Close close; // OPTIONAL
	GaDataSourceContext *context;
	ga_bool threadsafe;
} GaDataSourceCreationMinutiae;

/** Create a data source
 *
 * \return The created data source, or NULL if creation was unsuccessful
 */
GaDataSource *ga_data_source_create(const GaDataSourceCreationMinutiae *minutiae);

/** Reads binary data from the data source.
 *
 *  \ingroup GaDataSource
 *  \param dataSrc Data source from which to read.
 *  \param dst Destination buffer into which bytes should be read.  Must
 *                be at least (size * count) bytes in size.
 *  \param size Size of a single element (in bytes).
 *  \param count Number of elements to read.
 *  \return Total number of bytes read into the destination buffer.
 */
ga_usize ga_data_source_read(GaDataSource *dataSrc, void *dst, ga_usize size, ga_usize count);

/** Seek to an offset within a data source.
 *
 *  \ingroup GaDataSource
 *  \param dataSrc Data source to seek within.
 *  \param offset Offset (in bytes) from the specified seek origin.
 *  \param whence Seek origin (see [\ref seekOrigins]).
 *  \return If seek succeeds, returns GA_OK, otherwise returns GA_ERR_GENERIC (invalid seek request).
 *  \warning Only data sources with GaDataAccessFlag_Seekable can have ga_data_source_seek() called on them.
*/
ga_result ga_data_source_seek(GaDataSource *dataSrc, ga_ssize offset, GaSeekOrigin whence);

/** Tells the current read position of a data source.
 *
 *  \ingroup GaDataSource
 *  \param dataSrc Data source to tell the read position of.
 *  \return The current data source read position.
 */
ga_usize ga_data_source_tell(GaDataSource *dataSrc);

/** Returns the bitfield of flags set for a data source (see \ref globDefs).
 *
 *  \ingroup GaDataSource
 *  \param dataSrc Data source whose flags should be retrieved.
 *  \return The bitfield of flags set for the data source.
 */
GaDataAccessFlags ga_data_source_flags(GaDataSource *dataSrc);

/** Acquires a reference for a data source.
 *
 *  Increments the data source's reference count by 1.
 *
 *  \ingroup GaDataSource
 *  \param dataSrc Data source whose reference count should be incremented.
 */
void ga_data_source_acquire(GaDataSource *dataSrc);

/** Releases a reference for a data source.
 *
 *  Decrements the data source's reference count by 1. When the last reference is
 *  released, the data source's resources will be deallocated.
 *
 *  \ingroup GaDataSource
 *  \param dataSrc Data source whose reference count should be decremented.
 *  \warning A client must never use a data source after releasing its reference.
 */
void ga_data_source_release(GaDataSource *dataSrc);


/*******************/
/*  Sample Source  */
/*******************/
/** Abstract sample source data structure and associated functions.
 *
 *  \ingroup external
 *  \defgroup GaSampleSource Sample Source
 *  \todo Design a clearer/better system for easily extending this data type.
 */

/** Abstract sample source data structure [\ref MULTI_CLIENT].
 *
 *  A sample source is a source of PCM audio samples. These samples are usually
 *  generated from a compatible data source or sample source, which is transformed
 *  or decoded into the resulting PCM audio data.
 *
 *  This object may only be used on the main thread.
 *
 *  \ingroup GaSampleSource
 */
typedef struct GaSampleSource GaSampleSource;

/** Abstract context for callers back */
typedef struct GaSampleSourceContext GaSampleSourceContext;

/** On-seek callback function.
 *
 *  A callback that gets called while reading a sample source, if the sample source
 *  seeks as part of the read. This callback is used to implement gapless looping
 *  features within the sample source pipeline.
 *
 *  \ingroup GaSampleSource
 *  \param sample The sample the sample source was at when the seek happened.
 *  \param delta The signed distance from the old position to the new position.
 *  \param seekContext The user-specified context provided in ga_sample_source_read().
 */
typedef void (*GaCbOnSeek)(ga_sint32 sample, ga_sint32 delta, void *seekContext); //todo delta → ssz, sample → usz
/** \ref ga_sample_source_read */
typedef ga_usize (*GaCbSampleSource_Read)(GaSampleSourceContext *context, void *dst, ga_usize num_samples,
				     GaCbOnSeek onseek, void *seek_ctx);
/** \ref ga_sample_source_end */
typedef ga_bool (*GaCbSampleSource_End)(GaSampleSourceContext *context);
/** \ref ga_sample_source_ready */
typedef ga_bool (*GaCbSampleSource_Ready)(GaSampleSourceContext *context, ga_usize num_samples);
/** \ref ga_sample_source_seek */
typedef ga_result (*GaCbSampleSource_Seek)(GaSampleSourceContext *context, ga_usize sample_offset);
/** \ref ga_sample_source_tell */
typedef ga_result (*GaCbSampleSource_Tell)(GaSampleSourceContext *context, ga_usize *samples, ga_usize *total_samples);
/** \ref ga_sample_source_release */
typedef void (*GaCbSampleSource_Close)(GaSampleSourceContext *context);

/** Specifies the creation of a sample source */
typedef struct {
	GaCbSampleSource_Read read;
	GaCbSampleSource_End end;
	GaCbSampleSource_Ready ready;   // OPTIONAL
	GaCbSampleSource_Seek seek;     // OPTIONAL, must come with tell
	GaCbSampleSource_Tell tell;     // OPTIONAL
	GaCbSampleSource_Close close;   // OPTIONAL
	GaSampleSourceContext *context;
	GaFormat format;
	ga_bool threadsafe;
} GaSampleSourceCreationMinutiae;

/** Create a sample source
 *
 * \return The created sample source, or NULL if creation was unsuccessful
 */
GaSampleSource *ga_sample_source_create(const GaSampleSourceCreationMinutiae *minutiae);

/** Reads samples from a samples source.
 *
 *  \ingroup GaSampleSource
 *  \param sample_src Sample source from which to read.
 *  \param dst Destination buffer into which samples should be read. Must
 *                be at least (num_samples * sample-size) bytes in size.
 *  \param num_samples Number of samples to read.
 *  \param onseek The on-seek callback function for this read operation.
 *  \param seek_ctx User-specified context for the on-seek function.
 *  \return Total number of bytes read into the destination buffer.
 */
ga_usize ga_sample_source_read(GaSampleSource *sample_src, void *dst, ga_usize num_samples,
                               GaCbOnSeek onseek, void *seek_ctx);

/** Checks whether a sample source has reached the end of the stream.
 *
 *  \ingroup GaSampleSource
 *  \param sample_src Sample source to check.
 *  \return Whether the sample source has reached the end of the stream.
 */
ga_bool ga_sample_source_end(GaSampleSource *sample_src);

/** Checks whether a sample source has at least a given number of available
 *  samples.
 *
 *  If the sample source has fewer than num_samples samples left before it
 *  finishes, this function will returns GA_TRUE regardless of the number of
 *  samples.
 *
 *  \ingroup GaSampleSource
 *  \param sample_src Sample source to check.
 *  \param num_samples The minimum number of samples required for the sample
 *                       source to be considered ready.
 *  \return Whether the sample source has at least a given number of available
 *          samples.
 */
ga_bool ga_sample_source_ready(GaSampleSource *sample_src, ga_usize num_samples);

/** Seek to an offset (in samples) within a sample source.
 *
 *  \ingroup GaSampleSource
 *  \param sample_src Sample source to seek within.
 *  \param sample_offset Offset (in samples) from the start of the sample stream.
 *  \return If seek succeeds, returns GA_OK, otherwise returns GA_ERR_GENERIC (invalid seek request).
 *  \warning Only sample sources with GaDataAccessFlag_Seekable can have ga_sample_source_seek()
 *           called on them.
 */
ga_result ga_sample_source_seek(GaSampleSource *sample_src, ga_usize sample_offset);

/** Tells the current sample number of a sample source.
 *
 *  \ingroup GaSampleSource
 *  \param sample_src Sample source to tell the current sample number of.
 *  \param samples If set, the current sample source number will be stored here.
 *  \param total_samples If set, the total number of samples in the sample
 *         source will be stored here.
 *  \return GA_OK iff the telling was successful.
 */
ga_result ga_sample_source_tell(GaSampleSource *sample_src, ga_usize *samples, ga_usize *total_samples);

/** Returns the bitfield of flags set for a sample source (see \ref globDefs).
 *
 *  \ingroup GaSampleSource
 *  \param sample_src Sample source whose flags should be retrieved.
 *  \return The bitfield of flags set for the sample source.
 */
GaDataAccessFlags ga_sample_source_flags(GaSampleSource *sample_src);

/** Retrieves the PCM sample format for a sample source.
 *
 *  \ingroup GaSampleSource
 *  \param sample_src Sample source whose format should should be retrieved.
 *  \param format This value will be set to the same sample format
 *                    as samples in the sample source. Output parameter.
 *  \todo Either return a copy of the format, or make it a const* return value.
 */
void ga_sample_source_format(GaSampleSource *sample_src, GaFormat *format);

/** Acquires a reference for a sample source.
 *
 *  Increments the sample source's reference count by 1.
 *
 *  \ingroup GaSampleSource
 *  \param sample_src Sample source whose reference count should be incremented.
 */
void ga_sample_source_acquire(GaSampleSource *sample_src);

/** Releases a reference for a sample source.
 *
 *  Decrements the sample source's reference count by 1. When the last reference is
 *  released, the sample source's resources will be deallocated and the sample
 *  source's 'close' callback will be called.
 *
 *  \ingroup GaSampleSource
 *  \param sample_src Sample source whose reference count should be decremented.
 *  \warning A client must never use a sample source after releasing its reference.
 */
void ga_sample_source_release(GaSampleSource *sample_src);


/************/
/*  Memory  */
/************/
/** Shared (reference-counted) memory data structure and associated functions.
 *
 *  \ingroup external
 *  \defgroup GaMemory Memory
 */

/** Shared memory object data structure [\ref MULTI_CLIENT].
 *
 *  As a way of sharing data between multiple client across multiple threads,
 *  this data structure allows for a safe internal copy of the data. This is
 *  used in the internal implementation of GaSound, and can also be used to
 *  play compressed audio directly from memory without having to read the data
 *  source from a high-latency I/O interface or needlessly duplicate the data.
 *
 *  This object may be created on a secondary thread, but may otherwise only
 *  be used on the main thread.
 *
 *  \ingroup GaMemory
 */
typedef struct GaMemory GaMemory;

/** Create a shared memory object.
 *
 *  The buffer specified by data is copied into a newly-allocated internal
 *  storage buffer. As such, you may safely free the data buffer passed into
 *  ga_memory_create() as soon as the function returns.
 *  The returned memory object has an initial reference count of 1.
 *
 *  \ingroup GaMemory
 *  \param data Data buffer to be copied into an internal data buffer.
 *  \param size Size (in bytes) of the provided data buffer.
 *  \return Newly-allocated memory object, containing an internal copy of the
 *          provided data buffer.  If the buffer is null, then the contents
 *          will be uninitialized instead.
 */
GaMemory *ga_memory_create(void *data, ga_usize size);

/** Create a shared memory object from the full contents of a data source.
 *
 *  The full contents of the data source specified by dataSource are copied
 *  into a newly-allocated internal storage buffer.
 *  The returned object has an initial reference count of 1.
 *
 *  \ingroup GaMemory
 *  \param dataSource Data source to be read into an internal data buffer.
 *  \return Newly-allocated memory object, containing an internal copy of the
 *          full contents of the provided data source.
 */
GaMemory *ga_memory_create_data_source(GaDataSource *dataSource);

/** Retrieve the size (in bytes) of a memory object's stored data.
 *
 *  \ingroup GaMemory
 *  \param mem Memory object whose stored data size should be retrieved.
 *  \return Size (in bytes) of the memory object's stored data.
 */
ga_usize ga_memory_size(GaMemory *mem);

/** Retrieve a pointer to a memory object's stored data.
 *
 *  \ingroup GaMemory
 *  \param mem Memory object whose stored data pointer should be retrieved.
 *  \return Pointer to the memory object's stored data.
 *  \warning Never manually free the pointer returned by this function.
 */
void *ga_memory_data(GaMemory *mem);

/** Acquires a reference for a memory object.
 *
 *  Increments the memory object's reference count by 1.
 *
 *  \ingroup GaMemory
 *  \param mem Memory object whose reference count should be incremented.
 */
void ga_memory_acquire(GaMemory *mem);

/** Releases a reference for a memory object.
 *
 *  Decrements the memory object's reference count by 1. When the last reference is
 *  released, the memory object's resources will be deallocated.
 *
 *  \ingroup GaMemory
 *  \param mem Memory object whose reference count should be decremented.
 *  \warning A client must never use a memory object after releasing its reference.
 */
void ga_memory_release(GaMemory *mem);


/***********/
/*  Sound  */
/***********/
/** Shared (reference-counted) sound data structure and associated functions.
 *
 *  \ingroup external
 *  \defgroup GaSound Sound
 */

/** Shared sound object data structure [\ref MULTI_CLIENT].
 *
 *  As a way of sharing sounds between multiple client across multiple threads,
 *  this data structure allows for a safe internal copy of the PCM data. The
 *  data buffer must contain only raw PCM data, not formatted or compressed
 *  in any other way. To cache or share any other data, use a GaMemory.
 *
 *  This object may be created on a secondary thread, but may otherwise only
 *  be used on the main thread.
 *
 *  \ingroup GaSound
 */
typedef struct GaSound GaSound;

/** Create a shared sound object.
 *
 *  The provided memory buffer must contain raw PCM data. The returned
 *  object has an initial reference count of 1. This function acquires a
 *  reference from the provided memory object.
 *
 *  \ingroup GaSound
 *  \param memory Shared memory object containing raw PCM data. This
 *  function acquires a reference from the provided memory object.
 *  \param format Format of the raw PCM data contained by memory.
 *  \return Newly-allocated sound object.
 */
GaSound *ga_sound_create(GaMemory *memory, GaFormat *format);

/** Create a shared memory object from the full contents of a sample source.
 *
 *  The full contents of the sample source specified by sampleSource are
 *  streamed into a newly-allocated internal memory object.
 *  The returned sound object has an initial reference count of 1.
 *
 *  \ingroup GaSound
 *  \param sample_src Sample source to be read into an internal data buffer.
 *  \return Newly-allocated memory object, containing an internal copy of the
 *          full contents of the provided data source.
 */
GaSound *ga_sound_create_sample_source(GaSampleSource *sample_src);

/** Retrieve a pointer to a sound object's stored data.
 *
 *  \ingroup GaSound
 *  \param sound Sound object whose stored data pointer should be retrieved.
 *  \return Pointer to the sound object's stored data.
 *  \warning Never manually free the pointer returned by this function.
 */
void *ga_sound_data(GaSound *sound);

/** Retrieve the size (in bytes) of a sound object's stored data.
 *
 *  \ingroup GaSound
 *  \param sound Sound object whose stored data size should be retrieved.
 *  \return Size (in bytes) of the sound object's stored data.
 */
ga_usize ga_sound_size(GaSound *sound);

/** Retrieve the number of samples in a sound object's stored PCM data.
 *
 *  \ingroup GaSound
 *  \param sound Sound object whose number of samples should be retrieved.
 *  \return Number of samples in the sound object's stored PCM data.
 */
ga_usize ga_sound_num_samples(GaSound *sound);

/** Retrieves the PCM sample format for a sound.
 *
 *  \ingroup GaSound
 *  \param sound Sound whose format should should be retrieved.
 *  \param format This value will be set to the same sample format
 *                    as samples in the sound. Output parameter.
 *  \todo Either return a copy of the format, or make it a const* return value.
 */
void ga_sound_format(GaSound *sound, GaFormat *format);

/** Acquires a reference for a sound object.
 *
 *  Increments the sound object's reference count by 1.
 *
 *  \ingroup GaSound
 *  \param sound Sound object whose reference count should be incremented.
 *  \todo Either return a copy of the format, or make it a const* return value.
 */
void ga_sound_acquire(GaSound *sound);

/** Releases a reference for a sound object.
 *
 *  Decrements the sound object's reference count by 1. When the last reference is
 *  released, the sound object's resources will be deallocated.
 *
 *  \ingroup GaSound
 *  \param sound Sound object whose reference count should be decremented.
 *  \warning A client must never use a sound object after releasing its reference.
 */
void ga_sound_release(GaSound *sound);


/************/
/*  Mixer  */
/************/
/** Multi-channel audio mixer data structure and associated functions.
 *
 *  \ingroup external
 *  \defgroup GaMixer Mixer
 */

/** Audio mixer data structure [\ref SINGLE_CLIENT].
 *
 *  The mixer mixes PCM samples from multiple audio handles into a single buffer
 *  of PCM samples. The mixer is responsible for applying handle parameters
 *  such as gain, pan, and pitch. The mixer has a fixed sample size and format
 *  that must be specified upon creation. Buffers passed in must be large enough
 *  to hold the specified number of samples of the specified format.
 *
 *  This object may only be used on the main thread.
 *
 *  \ingroup GaMixer
 */
typedef struct GaMixer GaMixer;

/** Creates a mixer object with the specified number and format of PCM samples.
 *
 *  \ingroup GaMixer
 *  \param format Format for the PCM samples produced by the buffer.
 *  \param num_samples Number of samples to be mixed at a time (must be a power-of-two).
 *  \return Newly-created mixer object.
 *  \warning The number of samples must be a power-of-two.
 *  \todo Remove the requirement that the buffer be a power-of-two in size.
 */
GaMixer *ga_mixer_create(GaFormat *format, ga_uint32 num_samples);

/** Suspends the mixer, preventing it from consuming any of its inputs.  If you
 ** attempt to mix from it in this state, it will produce all zeroes
 *
 *  \ingroup GaMixer
 *  \param mixer The mixer to be suspended
 *  \return GA_OK iff the mixer was not already suspended
 */
ga_result ga_mixer_suspend(GaMixer *mixer);

/** Unsuspends the mixer, allowing it to consume all of its inputs again
 *
 *  \ingroup GaMixer
 *  \param mixer The mixer to be unsuspended
 *  \return GA_OK iff the mixer was already suspended
 */
ga_result ga_mixer_unsuspend(GaMixer *mixer);

/** Retrieves the PCM sample format for a mixer object.
 *
 *  \ingroup GaMixer
 *  \param mixer Mixer whose format should should be retrieved.
 *  \param fmt Location where the resultant format will be stored.
 */
void ga_mixer_format(GaMixer *mixer, GaFormat *fmt);

/** Retrieve the number of samples in a mixer object's mix buffer.
 *
 *  \ingroup GaMixer
 *  \param mixer Mixer object whose number of samples should be retrieved.
 *  \return Number of samples in a mixer object's mix buffer.
 */
ga_uint32 ga_mixer_num_samples(GaMixer *mixer);

/** Mixes samples from all ready handles into a single output buffer.
 *
 *  The output buffer is generally presented directly to the device queue
 *  for playback.
 *
 *  \ingroup GaMixer
 *  \param mixer Mixer object whose handles' samples should be mixed.
 *  \param buffer An empty buffer into which the mixed samples should be
 *                    copied. The buffer must be large enough to hold the
 *                    mixer's number of samples in the mixer's sample format.
 *  \return Whether the mixer successfully mixed the data. GA_SUCCESS if the
 *          operation was successful, GA_ERROR_GENERIC if not.
 */
ga_result ga_mixer_mix(GaMixer *mixer, void *buffer);

/** Dispatches all pending finish callbacks.
 *
 *  This function should be called regularly. This function (like all other functions
 *  associated with this object) must be called from the main thread. All callbacks
 *  will be called on the main thread.
 *
 *  \ingroup GaMixer
 *  \param mixer Mixer object whose handles' finish callbacks should be dispatched.
 *  \return Whether the mixer successfully dispatched the callbacks. GA_SUCCESS if the
 *          operation was successful, GA_ERROR_GENERIC if not.
 */
ga_result ga_mixer_dispatch(GaMixer *mixer);

/** Destroys a mixer object.
 *
 *  \ingroup GaMixer
 *  \param mixer Mixer object to destroy.
 *  \return Whether the mixer was successfully destroyed. GA_SUCCESS if the
 *          operation was successful, GA_ERROR_GENERIC if not.
 *  \warning The client must never use a mixer after calling ga_mixer_destroy().
 */
ga_result ga_mixer_destroy(GaMixer *mixer);


/************/
/*  Handle  */
/************/
/** Audio playback control handle data structure and associated functions.
 *
 *  Handle objects let you control pitch/pan/gain on playing audio, as well
 *  as playback state (play/pause/stop).
 *
 *  \ingroup external
 *  \defgroup GaHandle Handle
 */

/** Audio playback control handle data structure [\ref SINGLE_CLIENT].
 *
 *  A handle is a set of controls for manipulating the playback and mix
 *  parameters of a sample source.
 *
 *  Handles track playback parameters such as pan, pitch, and gain (volume)
 *  which are taken into account during the mix. Additionally, each handle
 *  tracks its playback state (playing/stopped/finished).
 *
 *  This object may only be used on the main thread.
 *
 *  \ingroup GaHandle
 */
typedef struct GaHandle GaHandle;

/** Enumerated handle parameter values.
 *
 *  Used when calling \ref ga_handle_set_paramf() "ga_handle_set_param*()"
 *  or \ref ga_handle_get_paramf() "ga_handle_get_param*()".
 *
 *  \ingroup GaHandle
 *  \defgroup handleParams Handle Parameters
 */
typedef enum {
	GaHandleParam_Unknown,  /**< Unknown parameter. \ingroup handleParams */
	GaHandleParam_Pan,      /**< Left <-> right pan (center -> 0.0, left -> -1.0, right -> 1.0). Floating-point parameter. \ingroup handleParams */
	GaHandleParam_Pitch,    /**< Pitch/speed multiplier (normal -> 1.0). Floating-point parameter. \ingroup handleParams */
	GaHandleParam_Gain,     /**< Gain/volume (silent -> 0.0, normal -> 1.0). Floating-point parameter. \ingroup handleParams */
} GaHandleParam;

/** Enumerated parameter values for ga_handle_tell().
 *
 *  Used in ga_handle_tell() to specify which value to return.
 *
 *  \ingroup GaHandle
 *  \defgroup tellParams Tell Parameters
 */
typedef enum {
	GaTellParam_Current,  /**< Current playback position (in samples). \ingroup tellParams */
	GaTellParam_Total,    /**< Total samples in this handle's sample source. \ingroup tellParams */
} GaTellParam;

/** Prototype for handle-finished-playback callback.
 *
 *  This callback will be called when the internal sampleSource ends. Stopping a handle
 *  does not generate this callback. Looping sample sources will never generate this
 *  callback.
 *
 *  \ingroup GaHandle
 *  \param finishedHandle The handle that has finished playback.
 *  \param context The user-specified callback context.
 *  \warning This callback is thrown once the handle has finished playback,
 *           after which the handle can no longer be used except to destroy it.
 *  \todo Allow handles with GaDataAccessFlag_Seekable to be rewound/reused once finished.
 */
typedef void (*GaCbHandleFinish)(GaHandle *finishedHandle, void *context);

/** Creates an audio playback control handle.
 *
 *  The sample source is not playing by default. To start playback, you must
 *  call ga_handle_play(). Default gain is 1.0. Default pan is 0.0. Default
 *  pitch is 1.0.
 *
 *  \ingroup GaHandle
 *  \param mixer The mixer that should mix the handle's sample data.
 *  \param sample_src The sample source from which to stream samples.
 *  \todo Provide a way to query handles for flags.
 */
GaHandle *ga_handle_create(GaMixer *mixer, GaSampleSource *sample_src);

/** Destroys an audio playback handle.
 *
 *  \ingroup GaHandle
 *  \param handle Handle object to destroy.
 *  \return Whether the mixer was successfully destroyed. GA_SUCCESS if the
 *          operation was successful, GA_ERROR_GENERIC if not.
 *  \warning The client must never use a handle after calling ga_handle_destroy().
 */
ga_result ga_handle_destroy(GaHandle *handle);

/** Starts playback on an audio playback handle.
 *
 *  It is valid to call ga_handle_play() on a handle that is already playing.
 *
 *  \ingroup GaHandle
 *  \param handle Handle object to play.
 *  \return Whether the handle played successfully. GA_SUCCESS if the
 *          operation was successful, GA_ERROR_GENERIC if not.
 *  \warning You cannot play a handle that has finished playing. When in doubt, check
 *           ga_handle_finished() to verify this state prior to calling play.
 */
ga_result ga_handle_play(GaHandle *handle);

/** Stops playback of a playing audio playback handle.
 *
 *  It is valid to call ga_handle_stop() on a handle that is already stopped.
 *
 *  \ingroup GaHandle
 *  \param handle Handle object to stop.
 *  \return Whether the handle was stopped successfully. GA_SUCCESS if the
 *          operation was successful, GA_ERROR_GENERIC if not.
 *  \warning You cannot stop a handle that has finished playing. When in doubt, check
 *           ga_handle_finished() to verify this state prior to calling play.
 */
ga_result ga_handle_stop(GaHandle *handle);

/** Checks whether a handle is currently playing.
 *
 *  \ingroup GaHandle
 *  \param handle Handle object to check.
 *  \return Whether the handle is currently playing.
 */
ga_bool ga_handle_playing(GaHandle *handle);

/** Checks whether a handle is currently stopped.
 *
 *  \ingroup GaHandle
 *  \param handle Handle object to check.
 *  \return Whether the handle is currently stopped.
 */
ga_bool ga_handle_stopped(GaHandle *handle);

/** Checks whether a handle is currently finished.
 *
 *  \ingroup GaHandle
 *  \param handle Handle object to check.
 *  \return Whether the handle is currently finished.
 */
ga_bool ga_handle_finished(GaHandle *handle);

/** Checks whether a handle is currently destroyed.
 *
 *  \ingroup GaHandle
 *  \param handle Handle object to check.
 *  \return Whether the handle is currently destroyed.
 */
ga_bool ga_handle_destroyed(GaHandle *handle);

/** Sets the handle-finished-playback callback for a handle.
 *
 *  This callback will be called right before the handle enters the 'finished'
 *  playback state. The callback value is set to 0 internally once it triggers.
 *
 *  \ingroup GaHandle
 *  \param handle Handle object to set the callback for.
 *  \param callback Callback function pointer.
 *  \param context User-specified callback context.
 *  \return Whether the handle's callback was set successfully. GA_SUCCESS if the
 *          operation was successful, GA_ERROR_GENERIC if not.
 */
ga_result ga_handle_set_callback(GaHandle *handle,
                                GaCbHandleFinish callback,
                                void *context);

/** Sets a floating-point parameter value on a handle.
 *
 *  \ingroup GaHandle
 *  \param handle Handle on which the parameter should be set
 *                   (see \ref handleParams).
 *  \param param Parameter to set (must be a floating-point value, see
 *                  \ref handleParams).
 *  \param value Value to set.
 *  \return Whether the parameter was set successfully. GA_SUCCESS if the
 *          operation was successful, GA_ERROR_GENERIC if not.
 */
ga_result ga_handle_set_paramf(GaHandle *handle,
                              GaHandleParam param,
                              ga_float32 value);

/** Retrieves a floating-point parameter value from a handle.
 *
 *  \ingroup GaHandle
 *  \param handle Handle from which the parameter should be retrieved
 *                   (see \ref handleParams).
 *  \param param Parameter to retrieve (must be a floating-point value, see
 *                  \ref handleParams).
 *  \param value Output parameter. Retrieved parameter is copied into the
 *                   memory pointed at by this pointer.
 *  \return Whether the parameter was retrieved successfully. GA_SUCCESS if the
 *          operation was successful, GA_ERROR_GENERIC if not.
 */
ga_result ga_handle_get_paramf(GaHandle *handle,
                              GaHandleParam param,
                              ga_float32 *value);

/** Sets an integer parameter value on a handle.
 *
 *  \ingroup GaHandle
 *  \param handle Handle on which the parameter should be set (see \ref handleParams).
 *  \param param Parameter to set (must be an integer value, see
 *                  \ref handleParams).
 *  \param value Value to set. valid ranges vary depending by parameter
 *                  (see \ref handleParams).
 *  \return Whether the parameter was set successfully. GA_SUCCESS if the
 *          operation was successful, GA_ERROR_GENERIC if not.
 */
ga_result ga_handle_set_parami(GaHandle *handle,
                              GaHandleParam param,
                              ga_sint32 value);

/** Retrieves an integer parameter value from a handle.
 *
 *  \ingroup GaHandle
 *  \param handle Handle from which the parameter should be retrieved
 *                   (see \ref handleParams).
 *  \param param Parameter to retrieve (must be an integer value, see
 *                  \ref handleParams).
 *  \param value Output parameter. Retrieved parameter is copied into the
 *                   memory pointed at by this pointer.
 *  \return Whether the parameter was retrieved successfully. GA_SUCCESS if the
 *          operation was successful, GA_ERROR_GENERIC if not.
 */
ga_result ga_handle_get_parami(GaHandle *handle,
                              GaHandleParam param,
                              ga_sint32 *value);

/** Seek to an offset (in samples) within a handle.
 *
 *  \ingroup GaHandle
 *  \param handle Handle to seek within.
 *  \param sample_offset Offset (in samples) from the start of the handle.
 *  \return If seek succeeds, returns 0, otherwise returns -1 (invalid seek request).
 *  \warning Only handles containing sample sources with GaDataAccessFlag_Seekable can
 *           have ga_handle_seek() called on them.
 */
ga_result ga_handle_seek(GaHandle *handle, ga_usize sample_offset);

/** Tells the current playback sample number or total samples of a handle.
 *
 *  \ingroup GaHandle
 *  \param handle Handle to query.
 *  \param param Tell value to retrieve.
 *  \param out The result is stored here if the parameters were valid.
 *  \return GA_OK iff the parameters were valid.
 */
ga_result ga_handle_tell(GaHandle *handle, GaTellParam param, ga_usize *out);

/** Checks whether a handle has at least a given number of available samples.
 *
 *  If the handle has fewer than num_samples samples left before it finishes,
 *  this function will returns GA_TRUE regardless of the number of samples.
 *
 *  \ingroup GaHandle
 *  \param handle Handle to check.
 *  \param num_samples The minimum number of samples required for the handle
 *                       to be considered ready.
 *  \return Whether the handle has at least a given number of available samples.
 */
ga_bool ga_handle_ready(GaHandle *handle, ga_usize num_samples);

/** Retrieves the PCM sample format for a handle.
 *
 *  \ingroup GaHandle
 *  \param handle Handle whose format should should be retrieved.
 *  \param format This value will be set to the same sample format
 *                    as samples streamed by the handle. Output parameter.
 *  \todo Either return a copy of the format, or make it a const* return value.
 */
void ga_handle_format(GaHandle *handle, GaFormat *format);


/*****************************/
/*  Buffered-Stream Manager  */
/*****************************/
/** Buffered-stream manager data structure and related functions.
 *
 *  \defgroup GaStreamManager Buffered Stream Manager
 *  \ingroup external
 */

/** Buffered-stream manager data structure [\ref SINGLE_CLIENT].
 *
 *  Manages a list of buffered streams, filling them. This class can be used
 *  on a background thread, to allow filling the streams without causing
 *  real-time applications to stutter.
 *
 *  \ingroup GaStreamManager
 */
typedef struct GaStreamManager GaStreamManager;

/** Creates a buffered-stream manager.
 *
 *  \ingroup GaStreamManager
 *  \return Newly-created stream manager.
 */
GaStreamManager *ga_stream_manager_create(void);

/** Fills all buffers managed by a buffered-stream manager.
 *
 *  \ingroup GaStreamManager
 *  \param mgr The buffered-stream manager whose buffers are to be filled.
 */
void ga_stream_manager_buffer(GaStreamManager *mgr);

/** Destroys a buffered-stream manager.
 *
 *  \ingroup GaStreamManager
 *  \param mgr The buffered-stream manager to be destroyed.
 *  \warning Never use a buffered-stream manager after it has been destroyed.
 */
void ga_stream_manager_destroy(GaStreamManager *mgr);


/*********************/
/*  Buffered Stream  */
/*********************/
/** Buffered stream data structure and related functions.
 *
 *  \defgroup GaBufferedStream Buffered Stream
 *  \ingroup external
 */

/** Buffered stream data structure [\ref MULTI_CLIENT].
 *
 *  Buffered streams read samples from a sample source into a buffer. They
 *  support seeking, reading, and all other sample source functionality,
 *  albeit through a different interface. This is done to decouple the
 *  background-streaming logic from the audio-processing pipeline logic.
 *
 *  \ingroup GaBufferedStream
 */
typedef struct GaBufferedStream GaBufferedStream;

/** Creates a buffered stream.
 *
 *  \ingroup GaBufferedStream
 *  \param mgr Buffered-stream manager to manage the buffered stream
 *                (non-optional).
 *  \param src Sample source to buffer samples from.
 *  \param buffer_size Size of the internal data buffer (in bytes). Must be
 *         a multiple of sample size.
 *  \return Newly-created buffered stream.
 *  \todo Change buffer_size to buffer_samples for a more fault-resistant
 *        interface.
 */
GaBufferedStream *ga_stream_create(GaStreamManager *mgr, GaSampleSource *src, ga_usize buffer_size);

/** Buffers samples from the sample source into the internal buffer (producer).
 *
 *  Can be called from a background thread.
 *
 *  \ingroup GaBufferedStream
 *  \param stream Stream to produce samples.
 *  \warning This function should only ever be called by the buffered stream
 *           manager.
 */
void ga_stream_produce(GaBufferedStream *stream); /* Can be called from a secondary thread */

/** Reads samples from a buffered stream.
 *
 *  \ingroup GaBufferedStream
 *  \param stream Buffered stream from which to read.
 *  \param dst Destination buffer into which samples should be read. Must
 *                be at least (num_samples * sample size) bytes in size.
 *  \param num_samples Number of samples to read.
 *  \return Total number of bytes read into the destination buffer.
 */
ga_usize ga_stream_read(GaBufferedStream *stream, void *dst, ga_usize num_samples);

/** Checks whether a buffered stream has reached the end of the stream.
 *
 *  \ingroup GaBufferedStream
 *  \param stream Buffered stream to check.
 *  \return Whether the buffered stream has reached the end of the stream.
 */
ga_bool ga_stream_end(GaBufferedStream *stream);

/** Checks whether a buffered stream has at least a given number of available
 *  samples.
 *
 *  If the sample source has fewer than num_samples samples left before it
 *  finishes, this function will returns GA_TRUE regardless of the number of
 *  samples.
 *
 *  \ingroup GaBufferedStream
 *  \param stream Buffered stream to check.
 *  \param num_samples The minimum number of samples required for the
 *                       buffered stream to be considered ready.
 *  \return Whether the buffered stream has at least a given number of available
 *          samples.
 */
ga_bool ga_stream_ready(GaBufferedStream *stream, ga_usize num_samples);

/** Seek to an offset (in samples) within a buffered stream.
 *
 *  \ingroup GaBufferedStream
 *  \param stream Buffered stream to seek within.
 *  \param sample_offset Offset (in samples) from the start of the contained
 *                         sample source.
 *  \return If seek succeeds, returns GA_OK, otherwise returns GA_ERR_GENERIC (invalid seek
 *          request).
 *  \warning Only buffered streams with GaDataAccessFlag_Seekable can have ga_stream_seek()
 *           called on them.
 */
ga_result ga_stream_seek(GaBufferedStream *stream, ga_usize sample_offset);

/** Tells the current sample number of a buffered stream.
 *
 *  \ingroup GaBufferedStream
 *  \param stream Buffered stream to tell the current sample number of.
 *
 *  \param samples If set, the current sample source sample number will be stored here.
 *  \param total_samples If set, the total number of samples in the contained sample
 *         source will be stored here.
 *  \return GA_OK iff the telling was successful
 */
ga_result ga_stream_tell(GaBufferedStream *stream, ga_usize *samples, ga_usize *total_samples);

/** Returns the bitfield of flags set for a buffered stream (see \ref globDefs).
 *
 *  \ingroup GaBufferedStream
 *  \param stream Buffered stream whose flags should be retrieved.
 *  \return The bitfield of flags set for the buffered stream.
 */
GaDataAccessFlags ga_stream_flags(GaBufferedStream *stream);

/** Acquire a reference for a buffered stream.
 *
 *  Increments the buffered stream's reference count by 1.
 *
 *  \ingroup GaBufferedStream
 *  \param stream Buffered stream whose reference count should be incremented.
 *  \todo Either return a copy of the format, or make it a const* return value.
 */
void ga_stream_acquire(GaBufferedStream *stream);

/** Releases a reference for a buffered stream.
 *
 *  Decrements the buffered stream's reference count by 1. When the last reference is
 *  released, the buffered stream's resources will be deallocated.
 *
 *  \ingroup GaBufferedStream
 *  \param stream Buffered stream whose reference count should be decremented.
 *  \warning A client must never use a buffered stream after releasing its reference.
 */
void ga_stream_release(GaBufferedStream *stream);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // GORILLA_GA_H

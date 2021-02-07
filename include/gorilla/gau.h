/** Gorilla Audio Utility API.
 *
 *  Utility data structures and functions to enhance Gorilla Audio's functionality.
 *
 *  \file gau.h
 */

#ifndef _GORILLA_GAU_H
#define _GORILLA_GAU_H

#include "gorilla/ga.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Data structures and functions.
 *
 *  \defgroup utility Utility API (GAU)
 */

/*************/
/*  Manager  */
/*************/
/** High-level audio manager and associated functions.
 *
 *  \ingroup utility
 *  \defgroup GauManager Manager
 */

/** High-level audio manager.
 *
 *  \ingroup GauManager
 */
typedef struct GauManager GauManager;

/** Manager thread policies.
 *
 *  \ingroup GauManager
 *  \defgroup threadPolicy Thread Policies
 */
typedef enum {
	GauThreadPolicy_Unknown, /**< Unknown thread policy. \ingroup threadPolicy */
	GauThreadPolicy_Single,  /**< Single-threaded policy (does not use background threads). \ingroup threadPolicy */
	GauThreadPolicy_Multi,   /**< Multi-threaded mode (uses background threads). \ingroup threadPolicy */
} GauThreadPolicy;

/** Creates an audio manager.
 *
 *  \ingroup GauManager
 */
GauManager *gau_manager_create(void);

/** Creates an audio manager (customizable).
*
*  \ingroup GauManager
*/
GauManager *gau_manager_create_custom(GaDeviceType *dev_type,
                                       GauThreadPolicy thread_policy,
                                       ga_uint32 *num_buffers,
                                       ga_uint32 *num_samples);

/** Updates an audio manager.
 *
 *  \ingroup GauManager
 */
void gau_manager_update(GauManager *in_mgr);

/** Retrieves the internal mixer object from an audio manager.
 *
 *  \ingroup GauManager
 */
GaMixer *gau_manager_mixer(GauManager *in_mgr);

/** Retrieves the internal buffered stream manager object from an audio manager.
 *
 *  \ingroup GauManager
 */
GaStreamManager *gau_manager_stream_manager(GauManager *in_mgr);

/** Retrieves the internal device object from an audio manager.
 *
 *  \ingroup GauManager
 */
GaDevice *gau_manager_device(GauManager *in_mgr);

/** Destroys an audio manager.
 *
 *  \ingroup GauManager
 */
void gau_manager_destroy(GauManager *in_mgr);

/*****************************/
/**  Concrete Data Sources  **/
/*****************************/
/** Concrete data source implementations.
 *
 *  \ingroup utility
 *  \defgroup concreteData Concrete Data Sources
 */

/** Creates a data source of bytes from a file-on-disk.
 *
 *  \ingroup concreteData
 */
GaDataSource *gau_data_source_create_file(const char *in_filename);

/** Creates a data source of bytes from a subregion of a file-on-disk.
 *
 *  \ingroup concreteData
 */
GaDataSource *gau_data_source_create_file_arc(const char *in_filename, ga_usize in_offset, ga_usize in_size);

/** Creates a data source of bytes from a block of shared memory.
 *
 *  \ingroup concreteData
 */
GaDataSource *gau_data_source_create_memory(GaMemory *in_memory);

/*******************************/
/**  Concrete Sample Sources  **/
/*******************************/
/** Concrete sample source implementations.
 *
 *  \ingroup utility
 *  \defgroup concreteSample Concrete Sample Sources
 */

/** Creates a sample source of PCM samples from a WAVE file.
 *
 *  \ingroup concreteSample
 */
GaSampleSource *gau_sample_source_create_wav(GaDataSource *in_dataSrc);

/** Creates a sample source of PCM samples from a FLAC file.
 *
 *  \ingroup concreteSample
 */
GaSampleSource *gau_sample_source_create_flac(GaDataSource *in_dataSrc);

/** Creates a sample source of PCM samples from an Ogg/Opus file.
 *
 *  \ingroup concreteSample
 */
GaSampleSource *gau_sample_source_create_opus(GaDataSource *in_dataSrc);

/** Creates a sample source of PCM samples from an Ogg/Vorbis file.
 *
 *  \ingroup concreteSample
 */
GaSampleSource *gau_sample_source_create_vorbis(GaDataSource *in_dataSrc);

/** Creates a buffered sample source of PCM samples from another sample source.
 *
 *  \ingroup concreteSample
 */
GaSampleSource *gau_sample_source_create_buffered(GaStreamManager *in_mgr, GaSampleSource *in_sampleSrc, ga_sint32 in_bufferSamples);

/** Creates a sample source of PCM samples from a cached sound object.
 *
 *  \ingroup concreteSample
 */
GaSampleSource *gau_sample_source_create_sound(GaSound *in_sound);

/** Creates a sample source of PCM samples from a stream.
 *
 *  \ingroup concreteSample
 */
GaSampleSource *gau_sample_source_create_stream(GaStreamManager *mgr, GaSampleSource *sample_src, ga_usize buffer_samples);

/**************************/
/**  Loop Sample Source  **/
/**************************/
/** Loop sample source.
 *
 *  \ingroup concreteSample
 *  \defgroup loopSample Loop Sample Source
 */

/** Loop sample source.
 *
 *  Sample source that controls looping behavior of a contained sample source.
 *
 *  \ingroup loopSample
 */
typedef struct GauSampleSourceLoop GauSampleSourceLoop;

/** Create a loop sample source.
 *
 *  \ingroup loopSample
 */
GauSampleSourceLoop *gau_sample_source_create_loop(GaSampleSource *src);

/** Retrieve the newly created sample source
 *
 * \ingroup loopSample
 */
GaSampleSource *gau_sample_source_loop_sample_source(GauSampleSourceLoop *s);

/** Set loop points on a loop sample source.  Resets the source's loop counter.
 *
 *  \ingroup loopSample
 *  \param trigger_sample upon reaching this sample, jump to the target sample.  Make this negative to loop upon reaching the end
 *  \param loop_enable set to false to disable looping and proxy directly to the underlying sample source
 */
void gau_sample_source_loop_set(GauSampleSourceLoop *src, ga_ssize trigger_sample, ga_usize target_sample, ga_bool loop_enable);

/** Disable looping on a loop sample source.
 *
 *  \ingroup loopSample
 */
void gau_sample_source_loop_disable(GauSampleSourceLoop *src);

/** Enable looping on a loop sample source.
 *
 *  \ingroup loopSample
 */
void gau_sample_source_loop_enable(GauSampleSourceLoop *src);

/** Count number of times a loop sample source has looped.
 *
 *  \ingroup loopSample
 */
ga_uint32 gau_sample_source_loop_count(GauSampleSourceLoop *src);

/***************************/
/**  On-Finish Callbacks  **/
/***************************/
/** Generic on-finish callbacks.
*
*  \ingroup utility
*  \defgroup onFinish On-Finish Callbacks
*/

/** On-finish callback that destroys the handle.
 *
 *  \ingroup onFinish
 */
void gau_on_finish_destroy(GaHandle *in_finishedHandle, void *in_context);

/********************/
/**  Load Helpers  **/
/********************/
/** Functions that help load common sources of data into cached memory.
 *
 *  \ingroup utility
 *  \defgroup loadHelper Load Helpers
 */

/** Load a file's raw binary data into a memory object.
 *
 *  \ingroup loadHelper
 */
GaMemory *gau_load_memory_file(const char *in_filename);


typedef enum {
	GauAudioType_Unknown,
	GauAudioType_Autodetect,
	GauAudioType_Wav,
	GauAudioType_Flac,
	GauAudioType_Opus,
	GauAudioType_Vorbis,
} GauAudioType;

/** Load a file's PCM data into a sound object.
 *
 *  \ingroup loadHelper
 */
GaSound *gau_load_sound_file(const char *in_filename, GauAudioType in_format);


/**********************/
/**  Create Helpers  **/
/**********************/
/** Functions that help to create common handle configurations.
*
*  \ingroup utility
*  \defgroup createHelper Create Helpers
*/

/** Create a handle to play a memory object in a given data format.
 *
 *  \ingroup createHelper
 */
GaHandle *gau_create_handle_memory(GaMixer *mixer, GaMemory *memory, GauAudioType format,
                                    GaCbHandleFinish callback, void *context,
                                    GauSampleSourceLoop **loop_src);

/** Create a handle to play a sound object.
 *
 *  \ingroup createHelper
 */
GaHandle *gau_create_handle_sound(GaMixer *mixer, GaSound *sound,
                                   GaCbHandleFinish callback, void *context,
                                   GauSampleSourceLoop **loop_src);

/** Create a handle to play a background-buffered stream from a sample source.
 *
 *  \ingroup createHelper
 */
GaHandle *gau_create_handle_buffered_samples(GaMixer *mixer, GaStreamManager *stream_mgr, GaSampleSource *src,
                                           GaCbHandleFinish callback, void *context,
                                           GauSampleSourceLoop **loop_src);

/** Create a handle to play a background-buffered stream from a data source.
 *
 *  \ingroup createHelper
 */
GaHandle *gau_create_handle_buffered_data(GaMixer *mixer, GaStreamManager *streamMgr,
                                           GaDataSource *data_src, GauAudioType format,
                                           GaCbHandleFinish callback, void *context,
                                           GauSampleSourceLoop **loop_src);

/** Create a handle to play a background-buffered stream from a file.
 *
 *  \ingroup createHelper
 */
GaHandle *gau_create_handle_buffered_file(GaMixer *mixer, GaStreamManager *stream_mgr,
                                           const char *filename, GauAudioType format,
                                           GaCbHandleFinish callback, void *context,
                                           GauSampleSourceLoop **loop_src);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _GORILLA_GAU_H */

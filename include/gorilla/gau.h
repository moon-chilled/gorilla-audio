/** Gorilla Audio Utility API.
 *
 *  Utility data structures and functions to enhance Gorilla Audio's functionality.
 *
 *  \file gau.h
 */

#ifndef _GORILLA_GAU_H
#define _GORILLA_GAU_H

#include "gorilla/common/ga_common.h"
#include "gorilla/ga.h"
#include "gorilla/ga_internal.h"

#ifdef __cplusplus
extern "C"
{
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
GauManager* gau_manager_create(void);

/** Creates an audio manager (customizable).
*
*  \ingroup GauManager
*/
GauManager* gau_manager_create_custom(GaDeviceType *dev_type,
                                       GauThreadPolicy thread_policy,
                                       gc_uint32 *num_buffers,
                                       gc_uint32 *num_samples);

/** Updates an audio manager.
 *
 *  \ingroup GauManager
 */
void gau_manager_update(GauManager* in_mgr);

/** Retrieves the internal mixer object from an audio manager.
 *
 *  \ingroup GauManager
 */
GaMixer* gau_manager_mixer(GauManager* in_mgr);

/** Retrieves the internal buffered stream manager object from an audio manager.
 *
 *  \ingroup GauManager
 */
GaStreamManager* gau_manager_stream_manager(GauManager* in_mgr);

/** Retrieves the internal device object from an audio manager.
 *
 *  \ingroup GauManager
 */
GaDevice* gau_manager_device(GauManager* in_mgr);

/** Destroys an audio manager.
 *
 *  \ingroup GauManager
 */
void gau_manager_destroy(GauManager* in_mgr);

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
GaDataSource* gau_data_source_create_file(const char* in_filename);

/** Creates a data source of bytes from a subregion of a file-on-disk.
 *
 *  \ingroup concreteData
 */
GaDataSource* gau_data_source_create_file_arc(const char* in_filename, gc_size in_offset, gc_size in_size);

/** Creates a data source of bytes from a block of shared memory.
 *
 *  \ingroup concreteData
 */
GaDataSource* gau_data_source_create_memory(GaMemory* in_memory);

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
GaSampleSource* gau_sample_source_create_wav(GaDataSource* in_dataSrc);

/** Creates a sample source of PCM samples from an Ogg/Vorbis file.
 *
 *  \ingroup concreteSample
 */
GaSampleSource* gau_sample_source_create_ogg(GaDataSource* in_dataSrc);

/** Creates a buffered sample source of PCM samples from another sample source.
 *
 *  \ingroup concreteSample
 */
GaSampleSource* gau_sample_source_create_buffered(GaStreamManager* in_mgr, GaSampleSource* in_sampleSrc, gc_int32 in_bufferSamples);

/** Creates a sample source of PCM samples from a cached sound object.
 *
 *  \ingroup concreteSample
 */
GaSampleSource* gau_sample_source_create_sound(GaSound* in_sound);

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
GauSampleSourceLoop* gau_sample_source_create_loop(GaSampleSource* in_sampleSrc);

/** Set loop points on a loop sample source.
 *
 *  \ingroup loopSample
 */
void gau_sample_source_loop_set(GauSampleSourceLoop* in_sampleSrc, gc_int32 in_triggerSample, gc_int32 in_targetSample);

/** Clear loop points on a loop sample source.
 *
 *  \ingroup loopSample
 */
void gau_sample_source_loop_clear(GauSampleSourceLoop* in_sampleSrc);

/** Count number of times a loop sample source has looped.
 *
 *  \ingroup loopSample
 */
gc_int32 gau_sample_source_loop_count(GauSampleSourceLoop* in_sampleSrc);

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
void gau_on_finish_destroy(GaHandle* in_finishedHandle, void* in_context);

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
GaMemory* gau_load_memory_file(const char* in_filename);


typedef enum {
	GauAudioType_Unknown = 1,
	GauAudioType_Autodetect = 2, // TODO: support this
	GauAudioType_Ogg = 3,
	GauAudioType_Wav = 4,
} GauAudioType;

/** Load a file's PCM data into a sound object.
 *
 *  \ingroup loadHelper
 */
GaSound* gau_load_sound_file(const char* in_filename, GauAudioType in_format);


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
GaHandle* gau_create_handle_memory(GaMixer* in_mixer, GaMemory* in_memory, GauAudioType in_format,
                                    ga_FinishCallback in_callback, void* in_context,
                                    GauSampleSourceLoop** out_loopSrc);

/** Create a handle to play a sound object.
 *
 *  \ingroup createHelper
 */
GaHandle* gau_create_handle_sound(GaMixer* in_mixer, GaSound* in_sound,
                                   ga_FinishCallback in_callback, void* in_context,
                                   GauSampleSourceLoop** out_loopSrc);

/** Create a handle to play a background-buffered stream from a data source.
 *
 *  \ingroup createHelper
 */
GaHandle* gau_create_handle_buffered_data(GaMixer* in_mixer, GaStreamManager* in_streamMgr,
                                           GaDataSource* in_dataSrc, GauAudioType in_format,
                                           ga_FinishCallback in_callback, void* in_context,
                                           GauSampleSourceLoop** out_loopSrc);

/** Create a handle to play a background-buffered stream from a file.
 *
 *  \ingroup createHelper
 */
GaHandle* gau_create_handle_buffered_file(GaMixer* in_mixer, GaStreamManager* in_streamMgr,
                                           const char* in_filename, GauAudioType in_format,
                                           ga_FinishCallback in_callback, void* in_context,
                                           GauSampleSourceLoop** out_loopSrc);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _GORILLA_GAU_H */

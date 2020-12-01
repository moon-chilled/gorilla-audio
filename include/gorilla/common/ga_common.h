/** Gorilla Common API.
 *
 *  A collection of non-audio-specific classes that are common to most libraries.
 *
 *  \file ga_common.h
 */

/** Common data structures and functions.
 *
 *  \defgroup common Common API (GC)
 */

#ifndef _GORILLA_GA_COMMON_H
#define _GORILLA_GA_COMMON_H

#include "ga_types.h"
#include "ga_thread.h"

#ifdef __cplusplus
extern "C" {
#endif

/*************************/
/**  System Operations  **/
/*************************/
/** System operations.
 *
 *  \ingroup common
 *  \defgroup GaSystemOps System Operations
 */

/** System allocation policies [\ref POD].
 *
 *  \ingroup GcSystemOps
 */
typedef struct {
	void* (*allocFunc)(gc_size size);
	void* (*reallocFunc)(void *ptr, gc_size size);
	void (*freeFunc)(void *ptr);
} GaSystemOps;
extern GaSystemOps* gcX_ops;

/** Initialize the Gorilla library.
 *
 *  This must be called before any other functions in the library.
 *
 *  \ingroup GaSystemOps
 *  \param callbacks You may (optionally) pass in a GaSystemOps structure
 *                      to define custom allocation functions.  If you do not,
 *                      Gorilla will use standard ANSI C malloc/realloc/free
 *                      functions.
 *  \return GA_OK if library initialized successfully. GA_ERR_GENERIC
 *          if not.
 */
ga_result ga_initialize_systemops(GaSystemOps *callbacks);

/** Shutdown the Gorilla library.
 *
 *  Call this once you are finished using the library. After calling it,
 *  do not call any functions in the library.
 *
 *  \ingroup GaSystemOps
 *  \return GA_OK if the library shut down successfully. GA_ERR_GENERIC
 *          if not.
 */
ga_result ga_shutdown_systemops(void);

/***********************/
/**  Circular Buffer  **/
/***********************/
/** Circular Buffer.
 *
 *  \ingroup common
 *  \defgroup GaCircBuffer Circular Buffer
 */

/** Circular buffer object [\ref SINGLE_CLIENT].
 *
 *  A circular buffer object that is thread-safe for single producer/single
 *  consumer use cases. It assumes a single thread producing (writing)
 *  data, and a single thread consuming (reading) data. The producer and
 *  consumer threads may be the same thread.
 *
 *  \ingroup GaCircBuffer
 *  \warning While it can be read/written from two different threads, the
 *           object's memory management policy is Single Client, since there
 *           is only one owner responsible for creation/destruction of the
 *           thread.
 */
typedef struct {
	gc_uint8 *data;
	gc_size data_size;
	_Atomic gc_size next_avail;
	_Atomic gc_size next_free;
} GaCircBuffer;

/** Create a circular buffer object.
 *
 *  \ingroup GaCircBuffer
 */
GaCircBuffer* ga_buffer_create(gc_size size);

/** Destroy a circular buffer object.
 *
 *  \ingroup GaCircBuffer
 */
ga_result ga_buffer_destroy(GaCircBuffer* buffer);

/** Retrieve number of available bytes to read from a circular buffer object.
 *
 *  \ingroup GaCircBuffer
 */
gc_size ga_buffer_bytesAvail(GaCircBuffer* buffer);

/** Retrieve number of free bytes to write to a circular buffer object.
 *
 *  \ingroup GaCircBuffer
 */
gc_size ga_buffer_bytesFree(GaCircBuffer* buffer);

/** Retrieve write buffer(s) of free data in a circular buffer object.
 *
 *  \ingroup GaCircBuffer
 *  \warning You must call ga_buffer_produce() to tell the buffer how many
 *           bytes you wrote to it.
 *  \return the number of buffers gotten (0, 1, or 2)
 */
gc_uint8 ga_buffer_getFree(GaCircBuffer *buffer, gc_size num_bytes,
                           void **data1, gc_size *size1,
                           void **data2, gc_size *size2);

/** Write data to the circular buffer.
 *
 *  Easier-to-use than ga_buffer_getFree(), but less flexible.
 *
 *  \ingroup GaCircBuffer
 *  \warning You must call ga_buffer_produce() to tell the buffer how many
 *           bytes you wrote to it.
 */
ga_result ga_buffer_write(GaCircBuffer *buffer, void *data, gc_size num_bytes);

/** Retrieve read buffer(s) of available data in a circular buffer object.
 *
 *  \ingroup GaCircBuffer
 *  \warning You must call ga_buffer_consume() to tell the buffer how many
 *           bytes you read from it.
 *  \return same as ga_buffer_getFree
 */
gc_uint8 ga_buffer_getAvail(GaCircBuffer *buffer, gc_size num_bytes,
                             void **data1, gc_size *out_size1,
                             void **data2, gc_size *out_size2);

/** Read data from the circular buffer.
 *
 *  Easier-to-use than ga_buffer_getAvail(), but less flexible.
 *
 *  \ingroup GaCircBuffer
 *  \warning You must call ga_buffer_consume() to tell the buffer how many
 *           bytes you read from it.
 */
void ga_buffer_read(GaCircBuffer *buffer, void *data, gc_size num_bytes);

/** Tell the buffer that bytes have been written to it.
 *
 *  \ingroup GaCircBuffer
 */
void ga_buffer_produce(GaCircBuffer *buffer, gc_size num_bytes);

/** Tell the buffer that bytes have been read from it.
 *
 *  \ingroup GaCircBuffer
 */
void ga_buffer_consume(GaCircBuffer *buffer, gc_size num_bytes);

/***********************/
/**  Linked List  **/
/***********************/
/** Linked list data structure and associated functions.
 *
 *  \ingroup common
 *  \defgroup GaLink Linked List
 */

/** Linked list data structure [POD].
 *
 *  Intended usage: create a GaLink head link. Add and remove additional links
 *  as needed. To iterate, start with it = head->next. Each loop, it = it->next.
 *  Terminate when it == &head.
 *
 *  \ingroup GaLink
 */
typedef struct GaLink GaLink;
struct GaLink {
	GaLink *next;
	GaLink *prev;
	void *data;
};


/** Initializes a linked list head element.
 *
 *  \ingroup GaLink
 */
void ga_list_head(GaLink *head);

/** Adds a link to a linked list (initializes the link).
 *
 *  \ingroup GaLink
 */
void ga_list_link(GaLink *head, GaLink *link, void *data);

/** Removes a link from the linked list.
 *
 *  \ingroup GaLink
 */
void ga_list_unlink(GaLink *link);

#ifdef __cplusplus
} //extern "C"
#endif

#endif /* _GORILLA_GA_COMMON_H */

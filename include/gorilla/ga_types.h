/** Types.
 *
 *  Core type definitions.
 *
 *  \file ga_types.h
 *  \defgroup types Core types
 */

#ifndef GORILLA_GA_TYPES_H
#define GORILLA_GA_TYPES_H

#ifndef GORILLA_GA_H
# error Never include this file directly; include ga.h instead.
#endif

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/****************************/
/**  Primitive Data Types  **/
/****************************/
/** Data Types
 *
 *  \ingroup types
 *  \defgroup primitiveTypes Primitive Types
 */

typedef uint8_t          ga_uint8;
typedef uint16_t         ga_uint16;
typedef uint32_t         ga_uint32;
typedef uint64_t         ga_uint64;
typedef  int8_t          ga_sint8;
typedef  int16_t         ga_sint16;
typedef  int32_t         ga_sint32;
typedef  int64_t         ga_sint64;

#ifdef __cplusplus
typedef bool             ga_bool;
# define ga_true         true
# define ga_false        false
#else
typedef _Bool            ga_bool;
# define ga_true         ((ga_bool)1)
# define ga_false        ((ga_bool)0)
#endif

typedef size_t           ga_usize;
typedef ptrdiff_t        ga_ssize;
typedef float            ga_float32;
typedef double           ga_float64;
#define GA_SSIZE_MAX PTRDIFF_MAX
#define GA_USIZE_MAX SIZE_MAX
enum {
	GA_S8_MIN  =        -128,
	GA_S16_MIN =      -32768,
	GA_S32_MIN = -2147483648,
};

#define GA_U8_MAX         255
#define GA_S8_MAX         127
#define GA_U16_MAX      65535
#define GA_S16_MAX      32767
#define GA_U32_MAX 4294967295
#define GA_S32_MAX 2147483647

/*********************/
/**  Result Values  **/
/*********************/
/** Result Values
 *
 *  \ingroup types
 *  \defgroup results Result Values
 */

/**< Return type for the result of an operation.
 *
 * If you just want a quick description of an error and don't want to handle
 * each separately, check if it's >= than each of the
 * categories--GA_ERR_GENERIC, GA_ERR_MIS, GA_ERR_SYS, GA_ERR_FMT
 */
typedef enum {
	GA_OK = 0,              /**< Operation completed successfully.  This is kept at 0 so precocious callers can use !res in place of ga_isok(res). */
	GA_ERR_GENERIC = 1,     /**< Unspecified error. */
	GA_ERR_INTERNAL = 2,    /**< Gorilla is in an inconsistent state.  We will attempt to continue to operate as consistently as possible, but shenanigans may ensue. */

	GA_ERR_MIS = 1 << 30,   /**< MIS category: errors that result from API misuse. */
	GA_ERR_MIS_PARAM =      /**< Parameter was invalid (for example, attempted to open a file but the filename was null). */
	GA_ERR_MIS,
	GA_ERR_MIS_UNSUP,       /**< Requested operation was not supportted on the given object (for example, attempted to seek an unseekable data source). */

	GA_ERR_SYS = 1 << 29,   /**< SYS category: errors that result from interactions with tye system. */
	GA_ERR_SYS_IO =         /**< The system was unable to perform some requisite I/O operation. */
	GA_ERR_SYS,
	GA_ERR_SYS_MEM,         /**< Memory allocation failed.  This may be your fault, if you overrided the default allocator. */
	GA_ERR_SYS_LIB,         /**< An different unspecified error resulted from some necessary (unforunate) interaction with a system library. */
	GA_ERR_SYS_RUN,         /**< Under/overflowed output buffer.  These are not always possible to avoid and thus do not necessarily indicate a bug with ga, but if you are consistently getting these, report it; we may be misusing a system api. */

	GA_ERR_FMT = 1 << 28,   /**< FMT category/entry: errors that result from poorly-formatted external data */
} ga_result;

static inline ga_bool ga_isok(ga_result res) {
	return res == GA_OK;
}


/***********************/
/**  Circular Buffer  **/
/***********************/
/** Circular Buffer.
 *
 *  \ingroup types
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
typedef struct GaCircBuffer GaCircBuffer;

#ifndef __cplusplus //_Atomic not nice
struct GaCircBuffer {
	ga_uint8 *data;
	ga_usize data_size;
	_Atomic ga_usize next_avail;
	_Atomic ga_usize next_free;
};
#endif

/** Create a circular buffer object.
 *
 *  \ingroup GaCircBuffer
 */
GaCircBuffer *ga_buffer_create(ga_usize size);

/** Destroy a circular buffer object.
 *
 *  \ingroup GaCircBuffer
 */
ga_result ga_buffer_destroy(GaCircBuffer *buffer);

/** Retrieve number of available bytes to read from a circular buffer object.
 *
 *  \ingroup GaCircBuffer
 */
ga_usize ga_buffer_bytes_avail(GaCircBuffer *buffer);

/** Retrieve number of free bytes to write to a circular buffer object.
 *
 *  \ingroup GaCircBuffer
 */
ga_usize ga_buffer_bytes_free(GaCircBuffer *buffer);

/** Retrieve write buffer(s) of free data in a circular buffer object.
 *
 *  \ingroup GaCircBuffer
 *  \warning You must call ga_buffer_produce() to tell the buffer how many
 *           bytes you wrote to it.
 *  \return the number of buffers gotten (0, 1, or 2)
 */
ga_uint8 ga_buffer_get_free(GaCircBuffer *buffer, ga_usize num_bytes,
                            void **data1, ga_usize *size1,
                            void **data2, ga_usize *size2);

/** Write data to the circular buffer.
 *
 *  Easier-to-use than ga_buffer_getFree(), but less flexible.
 *
 *  \ingroup GaCircBuffer
 *  \warning You must call ga_buffer_produce() to tell the buffer how many
 *           bytes you wrote to it.
 */
ga_result ga_buffer_write(GaCircBuffer *buffer, void *data, ga_usize num_bytes);

/** Retrieve read buffer(s) of available data in a circular buffer object.
 *
 *  \ingroup GaCircBuffer
 *  \warning You must call ga_buffer_consume() to tell the buffer how many
 *           bytes you read from it.
 *  \return same as ga_buffer_getFree
 */
ga_uint8 ga_buffer_get_avail(GaCircBuffer *buffer, ga_usize num_bytes,
                             void **data1, ga_usize *out_size1,
                             void **data2, ga_usize *out_size2);

/** Read data from the circular buffer.
 *
 *  Easier-to-use than ga_buffer_getAvail(), but less flexible.
 *
 *  \ingroup GaCircBuffer
 *  \warning You must call ga_buffer_consume() to tell the buffer how many
 *           bytes you read from it.
 */
void ga_buffer_read(GaCircBuffer *buffer, void *data, ga_usize num_bytes);

/** Tell the buffer that bytes have been written to it.
 *
 *  \ingroup GaCircBuffer
 */
void ga_buffer_produce(GaCircBuffer *buffer, ga_usize num_bytes);

/** Tell the buffer that bytes have been read from it.
 *
 *  \ingroup GaCircBuffer
 */
void ga_buffer_consume(GaCircBuffer *buffer, ga_usize num_bytes);

/***********************/
/**    Linked List    **/
/***********************/
/** Linked list data structure and associated functions.
 *
 *  \ingroup types
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

/** Moves links from head_src to head_dst.
 *
 *  \ingroup GaLink
 */
void ga_list_merge(GaLink *head_dst, GaLink *head_src);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* GORILLA_GA_TYPES_H */

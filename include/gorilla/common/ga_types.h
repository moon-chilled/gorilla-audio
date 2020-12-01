/** Common Types.
 *
 *  Cross-platform primitive type definitions.
 *
 *  \file ga_types.h
 */

#ifndef _GORILLA_GA_TYPES_H
#define _GORILLA_GA_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

/****************************/
/**  Primitive Data Types  **/
/****************************/
/** Data Types
 *
 *  \ingroup common
 *  \defgroup dataTypes Data Types
 */
#include <stdint.h>
typedef uint8_t     gc_uint8;
typedef uint16_t    gc_uint16;
typedef uint32_t    gc_uint32;
typedef uint64_t    gc_uint64;
typedef int8_t      gc_int8;
typedef int16_t     gc_int16;
typedef int32_t     gc_int32;
typedef int64_t     gc_int64;

typedef _Bool       gc_bool;
#define gc_true     ((gc_bool)1)
#define gc_false    ((gc_bool)0)

#include <stddef.h>
typedef size_t      gc_size;
typedef ptrdiff_t   gc_ssize;
typedef float       gc_float32;
typedef double      gc_float64;
#define GC_SSIZE_MAX PTRDIFF_MAX
#define GC_SIZE_MAX SIZE_MAX

/*********************/
/**  Result Values  **/
/*********************/
/** Result Values
 *
 *  \ingroup common
 *  \defgroup results Result Values
 */

/**< Return type for the result of an operation. \ingroup results */
typedef enum {
	GA_OK = 0,       /**< Operation completed successfully.  Keep this at 0 so precocious callers can use !res to see if res was successful \ingroup results */
	GA_ERR_GENERIC,  /**< Operation failed with an unspecified error. \ingroup results */
} ga_result;

static inline gc_bool ga_isok(ga_result res) {
	return res == GA_OK;
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* _GORILLA_GA_TYPES_H */

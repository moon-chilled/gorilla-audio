/** Common Types.
 *
 *  Cross-platform primitive type definitions.
 *
 *  \file gc_types.h
 */

#ifndef _GORILLA_GC_TYPES_H
#define _GORILLA_GC_TYPES_H

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

/*
  WARNING: Do not typedef char or bool!
  (also, note that char != signed char != unsigned char)
  typedef char         char;
  typedef bool         bool;
*/

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

typedef gc_int32 gc_result; /**< Return type for the result of an operation. \ingroup results */

#define GC_SUCCESS 1 /**< Operation completed successfully. \ingroup results */
#define GC_ERROR_GENERIC -1 /**< Operation failed with an unspecified error. \ingroup results */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _GORILLA_GC_TYPES_H */

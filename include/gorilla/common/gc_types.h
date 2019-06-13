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

#if __STDC_VERSION__ >= 199901L
# include <stdint.h>
  typedef uint8_t   gc_uint8;
  typedef uint16_t  gc_uint16;
  typedef uint32_t  gc_uint32;
  typedef uint64_t  gc_uint64;
  typedef int8_t    gc_int8;
  typedef int16_t   gc_int16;
  typedef int32_t   gc_int32;
  typedef int64_t   gc_int64;
#elif defined(_WIN32)
  typedef unsigned char     gc_uint8;
  typedef unsigned short    gc_uint16;
  typedef unsigned int      gc_uint32;
  typedef unsigned __int64  gc_uint64;
  typedef signed char       gc_int8;
  typedef signed short      gc_int16;
  typedef signed int        gc_int32;
  typedef signed __int64    gc_int64;
#elif defined(__GNUC__)
  typedef unsigned char          gc_uint8;
  typedef unsigned short         gc_uint16;
  typedef unsigned int           gc_uint32;
  typedef unsigned long long int gc_uint64;
  typedef signed char            gc_int8;
  typedef signed short           gc_int16;
  typedef signed int             gc_int32;
  typedef signed long long int   gc_int64;
#else
# error Types not yet specified for this platform
#endif

#include <stddef.h>
typedef size_t   gc_size;
typedef float    gc_float32;
typedef double   gc_float64;

#ifdef _MSC_VER
# include <BaseTsd.h>
typedef SSIZE_T gc_ssize;
#else
# include <sys/types.h>
typedef ssize_t  gc_ssize;
#endif

/*********************/
/**  Result Values  **/
/*********************/
/** Result Values
 *
 *  \ingroup common
 *  \defgroup results Result Values
 */

typedef gc_int32 gc_result; /**< Return type for the result of an operation. \ingroup results */

#define GC_FALSE 0 /**< Result was false. \ingroup results */
#define GC_TRUE 1 /**< Result was true. \ingroup results */
#define GC_SUCCESS 1 /**< Operation completed successfully. \ingroup results */
#define GC_ERROR_GENERIC -1 /**< Operation failed with an unspecified error. \ingroup results */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _GORILLA_GC_TYPES_H */

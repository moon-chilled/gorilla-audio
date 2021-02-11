/** Gorilla System API.
 *
 *  A collection of non-audio-specific classes for interacting with the system.
 *
 *  \file ga_system.h
 */

/** Data structures and functions for interacting with an external system.
 *
 *  \defgroup system System API
 */

#ifndef _GORILLA_GA_SYSTEM_H
#define _GORILLA_GA_SYSTEM_H

#include "ga_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*************************/
/**  System Operations  **/
/*************************/
/** System operations.
 *
 *  \ingroup system
 *  \defgroup GaSystemOps System Operations
 */

/** System allocation policies [\ref POD].
 *
 *  \ingroup GcSystemOps
 */
typedef struct {
	void *(*alloc)(ga_usize size);
	void *(*realloc)(void *ptr, ga_usize size);
	void (*free)(void *ptr);
} GaSystemOps;

void *ga_alloc(ga_usize size);
void *ga_realloc(void *ptr, ga_usize size);
void ga_free(void *ptr);


/** Initialize the Gorilla library.
 *
 *  \ingroup GaSystemOps
 *  \param callbacks You may (optionally) pass in a GaSystemOps structure
 *                      to define custom allocation functions.  If you do not,
 *                      Gorilla will use standard ANSI C malloc/realloc/free
 *                      functions.
 *  \return GA_OK iff library initialized successfully.
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


/************/
/*  Thread  */
/************/
/** Thread data structure and associated functions.
 *
 *  \ingroup system
 *  \defgroup GaThread Threading
 */

/** Enumerated thread priorities.
 *
 *  \ingroup GaThread
 *  \defgroup threadPrio Thread Priorities
 */
typedef enum {
	GaThreadPriority_Normal,
	GaThreadPriority_Low,
	GaThreadPriority_High,
	GaThreadPriority_Highest,
} GaThreadPriority;

/** Thread function callback.
 *
 *  Threads execute functions. Those functions must match this prototype.
 *  Thread functions should return non-zero values if they encounter an
 *  error, zero if they terminate without error.
 *
 *  \ingroup GaThread
 *  \param context The user-specified thread context.
 *  \return GA_OK iff thread terminated without error.
 */
typedef ga_result (*GaCbThreadFunc)(void *context);

typedef struct GaThreadObj GaThreadObj;

/** Thread data structure [\ref SINGLE_CLIENT].
 *
 *  \ingroup GaThread
 */
typedef struct {
	GaCbThreadFunc thread_func;
	GaThreadObj *thread_obj;
	void *context;
	ga_sint32 id;
	GaThreadPriority priority;
	ga_uint32 stack_size;
} GaThread;

/** Creates a new thread.
 *
 *  The created thread will begin running immediately.
 *
 *  \ingroup GaThread
 */
GaThread *ga_thread_create(GaCbThreadFunc thread_func, void *context,
                            GaThreadPriority priority, ga_uint32 stack_size);

/** Joins a thread with the current thread.
 *
 *  \ingroup GaThread
 */
void ga_thread_join(GaThread *thread);

/** Signals a thread to wait for a specified time interval.
 *
 *  While the time interval is specified in milliseconds, different operating
 *  systems have different guarantees about the minimum time interval provided.
 *  If accurate sleep timings are desired, make sure the thread priority is set
 *  to GaThreadPriority_High or GaThreadPriority_Highest.
 *
 *  \ingroup GaThread
 */
void ga_thread_sleep(ga_uint32 ms);

/** Destroys a thread object.
 *
 *  \ingroup GaThread
 *  \warning This should usually only be called once the the thread has
 *           successfully joined with another thread.
 *  \warning Never use a thread after it has been destroyed.
 */
void ga_thread_destroy(GaThread *thread);

/***********/
/*  Mutex  */
/***********/
/** Mutual exclusion lock data structure and associated functions.
 *
 *  \ingroup system
 *  \defgroup GaMutex Mutex
 */

/** Mutual exclusion lock (mutex) thread synchronization primitive data structure [\ref SINGLE_CLIENT].
 *
 *  \ingroup GaMutex
 */
typedef struct {
	void *mutex;
} GaMutex;

/** Creates a mutex.
 *
 *  \ingroup GaMutex
 */
ga_result ga_mutex_create(GaMutex *res);

/** Locks a mutex.
 *
 *  In general, any lock should have a matching unlock().
 *
 *  \ingroup GaMutex
 *  \warning Do not lock a mutex on the same thread without first unlocking.
 */
void ga_mutex_lock(GaMutex mutex);

/** Unlocks a mutex.
 *
 *  \ingroup GaMutex
 *  \warning Do not unlock a mutex without first locking it.
 */
void ga_mutex_unlock(GaMutex mutex);

/** Destroys a mutex.
 *
 *  \ingroup GaMutex
 *  \warning Make sure the mutex is no longer in use before destroying it.
 *  \warning Never use a mutex after it has been destroyed.
 */
void ga_mutex_destroy(GaMutex mutex);

#ifdef __cplusplus
} //extern "C"
#endif

#endif /* _GORILLA_GA_SYSTEM_H */

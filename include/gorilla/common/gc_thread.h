/** Threads and Synchronization.
 *
 *  \file gc_thread.h
 */

#ifndef _GORILLA_GC_THREAD_H
#define _GORILLA_GC_THREAD_H

#include "gc_types.h"

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

/************/
/*  Thread  */
/************/
/** Thread data structure and associated functions.
 *
 *  \ingroup common
 *  \defgroup gc_Thread Thread
 */

/** Enumerated thread priorities.
 *
 *  \ingroup gc_Thread
 *  \defgroup threadPrio Thread Priorities
 */
#define GC_THREAD_PRIORITY_NORMAL 0 /**< Normal thread priority. \ingroup threadPrio */
#define GC_THREAD_PRIORITY_LOW 1 /**< Low thread priority. \ingroup threadPrio */
#define GC_THREAD_PRIORITY_HIGH 2 /**< High thread priority. \ingroup threadPrio */
#define GC_THREAD_PRIORITY_HIGHEST 3 /**< Highest thread priority. \ingroup threadPrio */

/** Thread function callback.
 *
 *  Threads execute functions. Those functions must match this prototype.
 *  Thread functions should return non-zero values if they encounter an
 *  error, zero if they terminate without error.
 *
 *  \ingroup gc_Thread
 *  \param context The user-specified thread context.
 *  \return GC_SUCCESS if thread terminated without error. GC_ERROR_GENERIC
 *          if not.
 */
typedef gc_int32 (*gc_ThreadFunc)(void* context);

/** Thread data structure [\ref SINGLE_CLIENT].
 *
 *  \ingroup gc_Thread
 */
typedef struct {
	gc_ThreadFunc thread_func;
	void *thread_obj;
	void *context;
	gc_int32 id;
	gc_int32 priority;
	gc_uint32 stack_size;
} gc_Thread;

/** Creates a new thread.
 *
 *  The created thread will not run until gc_thread_run() is called on it.
 *
 *  \ingroup gc_Thread
 */
gc_Thread* gc_thread_create(gc_ThreadFunc thread_func, void *context,
                            gc_int32 priority, gc_uint32 stack_size);

/** Runs a thread.
 *
 *  \ingroup gc_Thread
 */
void gc_thread_run(gc_Thread *thread);

/** Joins a thread with the current thread.
 *
 *  \ingroup gc_Thread
 */
void gc_thread_join(gc_Thread *thread);

/** Signals a thread to wait for a specified time interval.
 *
 *  While the time interval is specified in milliseconds, different operating
 *  systems have different guarantees about the minimum time interval provided.
 *  If accurate sleep timings are desired, make sure the thread priority is set
 *  to GC_THREAD_PRIORITY_HIGH or GC_THREAD_PRIORITY_HIGHEST.
 *
 *  \ingroup gc_Thread
 */
void gc_thread_sleep(gc_uint32 ms);

/** Destroys a thread object.
 *
 *  \ingroup gc_Thread
 *  \warning This should usually only be called once the the thread has
 *           successfully joined with another thread.
 *  \warning Never use a thread after it has been destroyed.
 */
void gc_thread_destroy(gc_Thread *thread);

/***********/
/*  Mutex  */
/***********/
/** Mutual exclusion lock data structure and associated functions.
 *
 *  \ingroup common
 *  \defgroup gc_Mutex Mutex
 */

/** Mutual exclusion lock (mutex) thread synchronization primitive data structure [\ref SINGLE_CLIENT].
 *
 *  \ingroup gc_Mutex
 */
typedef struct gc_Mutex {
	void *mutex;
} gc_Mutex;

/** Creates a mutex.
 *
 *  \ingroup gc_Mutex
 */
gc_Mutex *gc_mutex_create(void);

/** Locks a mutex.
 *
 *  In general, any lock should have a matching unlock().
 *
 *  \ingroup gc_Mutex
 *  \warning Do not lock a mutex on the same thread without first unlocking.
 */
void gc_mutex_lock(gc_Mutex *mutex);

/** Unlocks a mutex.
 *
 *  \ingroup gc_Mutex
 *  \warning Do not unlock a mutex without first locking it.
 */
void gc_mutex_unlock(gc_Mutex *mutex);

/** Destroys a mutex.
 *
 *  \ingroup gc_Mutex
 *  \warning Make sure the mutex is no longer in use before destroying it.
 *  \warning Never use a mutex after it has been destroyed.
 */
void gc_mutex_destroy(gc_Mutex *mutex);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _GORILLA_GC_H */

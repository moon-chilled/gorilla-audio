/** Threads and Synchronization.
 *
 *  \file ga_thread.h
 */

#ifndef _GORILLA_GA_THREAD_H
#define _GORILLA_GA_THREAD_H

#include "ga_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/************/
/*  Thread  */
/************/
/** Thread data structure and associated functions.
 *
 *  \ingroup common
 *  \defgroup GaThread Thread
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
 *  \return GA_OK if thread terminated without error. GA_ERR_GENERIC
 *          if not.
 */
typedef gc_int32 (*GaCbThreadFunc)(void* context);

/** Thread data structure [\ref SINGLE_CLIENT].
 *
 *  \ingroup GaThread
 */
typedef struct {
	GaCbThreadFunc thread_func;
	void *thread_obj;
	void *context;
	gc_int32 id;
	gc_int32 priority;
	gc_uint32 stack_size;
} GaThread;

/** Creates a new thread.
 *
 *  The created thread will not run until ga_thread_run() is called on it.
 *
 *  \ingroup GaThread
 */
GaThread* ga_thread_create(GaCbThreadFunc thread_func, void *context,
                            gc_int32 priority, gc_uint32 stack_size);

/** Runs a thread.
 *
 *  \ingroup GaThread
 */
void ga_thread_run(GaThread *thread);

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
void ga_thread_sleep(gc_uint32 ms);

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
 *  \ingroup common
 *  \defgroup GaMutex Mutex
 */

/** Mutual exclusion lock (mutex) thread synchronization primitive data structure [\ref SINGLE_CLIENT].
 *
 *  \ingroup GaMutex
 */
typedef struct GaMutex {
	void *mutex;
} GaMutex;

/** Creates a mutex.
 *
 *  \ingroup GaMutex
 */
GaMutex *ga_mutex_create(void);

/** Locks a mutex.
 *
 *  In general, any lock should have a matching unlock().
 *
 *  \ingroup GaMutex
 *  \warning Do not lock a mutex on the same thread without first unlocking.
 */
void ga_mutex_lock(GaMutex *mutex);

/** Unlocks a mutex.
 *
 *  \ingroup GaMutex
 *  \warning Do not unlock a mutex without first locking it.
 */
void ga_mutex_unlock(GaMutex *mutex);

/** Destroys a mutex.
 *
 *  \ingroup GaMutex
 *  \warning Make sure the mutex is no longer in use before destroying it.
 *  \warning Never use a mutex after it has been destroyed.
 */
void ga_mutex_destroy(GaMutex *mutex);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _GORILLA_GA_THREAD_H

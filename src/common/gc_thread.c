#include "gorilla/common/gc_common.h"

#include "gorilla/common/gc_thread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Thread Functions */

#if defined(_WIN32) || defined (__CYGWIN__)

#define WIN32_LEAN_AND_MEAN

#include <windows.h>

static gc_int32 priorityLut[] = {
	[GC_THREAD_PRIORITY_NORMAL] = 0,
	[GC_THREAD_PRIORITY_LOW] = -1,
	[GC_THREAD_PRIORITY_HIGH] = 1,
	[GC_THREAD_PRIORITY_HIGHEST] = 2,
};

gc_Thread* gc_thread_create(gc_ThreadFunc thread_func, void* context,
                            gc_int32 priority, gc_uint32 stack_size) {
	gc_Thread* ret = gcX_ops->allocFunc(sizeof(gc_Thread));
	ret->thread_obj = gcX_ops->allocFunc(sizeof(HANDLE));
	ret->thread_func = thread_func;
	ret->context = context;
	ret->priority = priority;
	ret->stack_size = stack_size;
	*(HANDLE*)ret->thread_obj = CreateThread(0, stack_size, (LPTHREAD_START_ROUTINE)thread_func, context, CREATE_SUSPENDED, (LPDWORD)&ret->id);
	SetThreadPriority(*(HANDLE*)ret->thread_obj, priorityLut[priority]);
	return ret;
}
void gc_thread_run(gc_Thread *thread) {
	ResumeThread(*(HANDLE*)thread->thread_obj);
}
void gc_thread_join(gc_Thread *thread) {
	WaitForSingleObject(*(HANDLE*)thread->thread_obj, INFINITE);
}
void gc_thread_sleep(gc_uint32 in_ms) {
	Sleep(in_ms);
}
void gc_thread_destroy(gc_Thread *thread) {
	CloseHandle(*(HANDLE*)thread->thread_obj);
	gcX_ops->freeFunc(thread->thread_obj);
	gcX_ops->freeFunc(thread);
}

#elif defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#include <pthread.h>
#include <sched.h>

static gc_int32 priorityLut[] = {
	[GC_THREAD_PRIORITY_NORMAL] = 0,
	[GC_THREAD_PRIORITY_LOW] = 19,
	[GC_THREAD_PRIORITY_HIGH] = -11,
	[GC_THREAD_PRIORITY_HIGHEST] = -20
};

typedef struct {
	pthread_t thread;
	pthread_attr_t attr;
	pthread_mutex_t suspend_mutex;
	gc_ThreadFunc thread_func;
	void *context;
} LinuxThreadData;

static void *StaticThreadWrapper(void *context) {
	LinuxThreadData* thread_data = (LinuxThreadData*)context;
	pthread_mutex_lock(&thread_data->suspend_mutex);
	thread_data->thread_func(thread_data->context);
	pthread_mutex_unlock(&thread_data->suspend_mutex);
	return 0;
}

gc_Thread* gc_thread_create(gc_ThreadFunc thread_func, void* context,
                            gc_int32 priority, gc_uint32 stack_size) {
	struct sched_param param;
	gc_Thread* ret = gcX_ops->allocFunc(sizeof(gc_Thread));
	if (!ret) goto fail;
	LinuxThreadData* thread_data = (LinuxThreadData*)gcX_ops->allocFunc(sizeof(LinuxThreadData));
	if (!thread_data) goto fail;
	thread_data->thread_func = thread_func;
	thread_data->context = context;

	ret->thread_obj = thread_data;
	ret->thread_func = thread_func;
	ret->context = context;
	ret->priority = priority;
	ret->stack_size = stack_size;

	pthread_mutex_init(&thread_data->suspend_mutex, NULL);
	pthread_mutex_lock(&thread_data->suspend_mutex);

	if (pthread_attr_init(&thread_data->attr) != 0){} // report error
#if defined(__APPLE__) || defined(__ANDROID__) || defined(__FreeBSD__) || defined(__OpenBSD__)
	param.sched_priority = priorityLut[priority];
#elif defined(__linux__)
	param.__sched_priority = priorityLut[priority];
#endif /* __APPLE__ */
	if (pthread_attr_setschedparam(&thread_data->attr, &param) != 0){} //report error
	if (pthread_attr_setstacksize(&thread_data->attr, stack_size) != 0){} //report error
	if (pthread_create(&thread_data->thread, &thread_data->attr, StaticThreadWrapper, thread_data) != 0) goto fail;

	return ret;
fail:
	if (ret) gcX_ops->freeFunc(ret->thread_obj);
	gcX_ops->freeFunc(ret);
	return NULL;
}
void gc_thread_run(gc_Thread *thread) {
	LinuxThreadData* thread_data = (LinuxThreadData*)thread->thread_obj;
	pthread_mutex_unlock(&thread_data->suspend_mutex);
}
void gc_thread_join(gc_Thread *thread) {
	LinuxThreadData* thread_data = (LinuxThreadData*)thread->thread_obj;
	pthread_join(thread_data->thread, 0);
}
void gc_thread_sleep(gc_uint32 in_ms) {
	usleep(in_ms * 1000);
}
void gc_thread_destroy(gc_Thread *thread) {
	LinuxThreadData* thread_data = (LinuxThreadData*)thread->thread_obj;
	pthread_cancel(thread_data->thread);
	pthread_join(thread_data->thread, NULL);
	pthread_mutex_destroy(&thread_data->suspend_mutex);
	pthread_attr_destroy(&thread_data->attr);
	gcX_ops->freeFunc(thread_data);
	gcX_ops->freeFunc(thread);
}

#else
#error Thread class not yet defined for this platform
#endif /* _WIN32 */

/* Mutex Functions */

#if defined(_WIN32) || defined(__CYGWIN__)

#define WIN32_LEAN_AND_MEAN

#include <windows.h>

gc_Mutex *gc_mutex_create() {
	gc_Mutex* ret = gcX_ops->allocFunc(sizeof(gc_Mutex));
	ret->mutex = gcX_ops->allocFunc(sizeof(CRITICAL_SECTION));
	InitializeCriticalSection((CRITICAL_SECTION*)ret->mutex);
	return ret;
}
void gc_mutex_destroy(gc_Mutex *mutex) {
	DeleteCriticalSection((CRITICAL_SECTION*)mutex->mutex);
	gcX_ops->freeFunc(mutex->mutex);
	gcX_ops->freeFunc(mutex);
}
void gc_mutex_lock(gc_Mutex *mutex) {
	EnterCriticalSection((CRITICAL_SECTION*)mutex->mutex);
}
void gc_mutex_unlock(gc_Mutex *mutex) {
	LeaveCriticalSection((CRITICAL_SECTION*)mutex->mutex);
}

#elif defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)

#include <pthread.h>

gc_Mutex* gc_mutex_create() {
	gc_Mutex* ret = gcX_ops->allocFunc(sizeof(gc_Mutex));
	ret->mutex = gcX_ops->allocFunc(sizeof(pthread_mutex_t));
	pthread_mutex_init((pthread_mutex_t*)ret->mutex, NULL);
	return ret;
}
void gc_mutex_destroy(gc_Mutex *mutex) {
	pthread_mutex_destroy((pthread_mutex_t*)mutex->mutex);
	gcX_ops->freeFunc(mutex->mutex);
	gcX_ops->freeFunc(mutex);
}
void gc_mutex_lock(gc_Mutex *mutex) {
	pthread_mutex_lock((pthread_mutex_t*)mutex->mutex);
}
void gc_mutex_unlock(gc_Mutex *mutex) {
	pthread_mutex_unlock((pthread_mutex_t*)mutex->mutex);
}

#else
#error Mutex class not yet defined for this platform
#endif /*  defined(_WIN32) || defined(__CYGWIN__) */

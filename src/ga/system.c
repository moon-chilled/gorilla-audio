#include "gorilla/ga_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Thread Functions */

#if (__STDC_VERSION__ >= 201112L) && !defined(__STDC_NO_THREADS__)

_Static_assert(sizeof(ga_result) == sizeof(int), "aliasing is illegal!");

#include <threads.h>

struct GaThreadObj {
	thrd_t t;
};

GaThread *ga_thread_create(GaCbThreadFunc thread_func, void *context,
                           GaThreadPriority priority, u32 stack_size) {
	GaThread *ret = ga_alloc(sizeof(GaThread));
	if (!ret) goto fail;
	ret->thread_obj = ga_alloc(sizeof(GaThread));
	if (!ret) goto fail;
	if (thrd_create(&ret->thread_obj->t, (int(*)(void*))thread_func, context) != thrd_success) goto fail;

	return ret;

fail:
	if (ret) ga_free(ret->thread_obj);
	ga_free(ret);
	return NULL;
}
void ga_thread_join(GaThread *thread) {
	thrd_join(thread->thread_obj->t, NULL);
}
void ga_thread_sleep(u32 ms) {
	thrd_sleep(&(struct timespec){.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000}, NULL); //todo resume/int
}
void ga_thread_destroy(GaThread *thread) {
	thrd_detach(thread->thread_obj->t); //pretty much the best we can do
	ga_free(thread->thread_obj);
	ga_free(thread);
}

ga_result ga_mutex_create(GaMutex *res) {
	res->mutex = ga_alloc(sizeof(mtx_t));
	if (!res->mutex) return GA_ERR_SYS_LIB;
	if (mtx_init(res->mutex, mtx_plain) != thrd_success) {
		ga_free(res->mutex);
		return GA_ERR_SYS_LIB;
	}
	return GA_OK;
}
void ga_mutex_lock(GaMutex mutex) {
	mtx_lock(mutex.mutex);
}
void ga_mutex_unlock(GaMutex mutex) {
	mtx_unlock(mutex.mutex);
}
void ga_mutex_destroy(GaMutex mutex) {
	if (!mutex.mutex) return;
	mtx_destroy(mutex.mutex);
	ga_free(mutex.mutex);
}

#elif defined(_WIN32) || defined (__CYGWIN__)

#define WIN32_LEAN_AND_MEAN

#include <windows.h>

static s32 priority_lut[] = {
	[GaThreadPriority_Normal] = 0,
	[GaThreadPriority_Low] = -1,
	[GaThreadPriority_High] = 1,
	[GaThreadPriority_Highest] = 2,
};

struct GaThreadObj {
	HANDLE h;
};

GaThread *ga_thread_create(GaCbThreadFunc thread_func, void *context,
                            GaThreadPriority priority, u32 stack_size) {
	GaThread *ret = ga_alloc(sizeof(GaThread));
	ret->thread_obj = ga_alloc(sizeof(GaThreadObj));
	ret->thread_func = thread_func;
	ret->context = context;
	ret->priority = priority;
	ret->stack_size = stack_size;
	ret->thread_obj->h = CreateThread(0, stack_size, (LPTHREAD_START_ROUTINE)thread_func, context, CREATE_SUSPENDED, (LPDWORD)&ret->id);
	SetThreadPriority(ret->thread_obj->h, priority_lut[priority]);
	ResumeThread(thread->thread_obj->h);
	return ret;
}
void ga_thread_join(GaThread *thread) {
	WaitForSingleObject(thread->thread_obj->h, INFINITE);
}
void ga_thread_sleep(u32 ms) {
	Sleep(ms);
}
void ga_thread_destroy(GaThread *thread) {
	CloseHandle(thread->thread_obj->h);
	ga_free(thread->thread_obj);
	ga_free(thread);
}

ga_result ga_mutex_create(GaMutex *es) {
	res->mutex = ga_alloc(sizeof(CRITICAL_SECTION));
	InitializeCriticalSection((CRITICAL_SECTION*)ret->mutex);
	return GA_OK;
}
void ga_mutex_destroy(GaMutex mutex) {
	if (!mutex.mutex) return;
	DeleteCriticalSection((CRITICAL_SECTION*)mutex.mutex);
	ga_free(mutex.mutex);
}
void ga_mutex_lock(GaMutex mutex) {
	EnterCriticalSection((CRITICAL_SECTION*)mutex.mutex);
}
void ga_mutex_unlock(GaMutex mutex) {
	LeaveCriticalSection((CRITICAL_SECTION*)mutex.mutex);
}

#elif defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__unix__) || defined(__POSIX__)
#include <pthread.h>
#include <sched.h>

static s32 priority_lut[] = {
	[GaThreadPriority_Normal] = 0,
	[GaThreadPriority_Low] = 19,
	[GaThreadPriority_High] = -11,
	[GaThreadPriority_Highest] = -20
};

struct GaThreadObj {
	pthread_t thread;
	pthread_attr_t attr;
};

GaThread *ga_thread_create(GaCbThreadFunc thread_func, void *context,
                           GaThreadPriority priority, u32 stack_size) {
	struct sched_param param;
	GaThread *ret = ga_alloc(sizeof(GaThread));
	if (!ret) goto fail;
	GaThreadObj *thread_obj = ga_alloc(sizeof(GaThreadObj));
	if (!thread_obj) goto fail;

	ret->thread_obj = thread_obj;
	ret->thread_func = thread_func;
	ret->context = context;
	ret->priority = priority;
	ret->stack_size = stack_size;

	if (pthread_attr_init(&thread_obj->attr) != 0){} // report error
#if defined(__APPLE__) || defined(__ANDROID__) || defined(__FreeBSD__) || defined(__OpenBSD__)
	param.sched_priority = priority_lut[priority];
#elif defined(__linux__)
	param.__sched_priority = priority_lut[priority];
#endif
	if (pthread_attr_setschedparam(&thread_obj->attr, &param) != 0){} //report error
	if (pthread_attr_setstacksize(&thread_obj->attr, stack_size) != 0){} //report error
	if (pthread_create(&thread_obj->thread, &thread_obj->attr, thread_func, context) != 0) goto fail;

	return ret;
fail:
	if (ret) ga_free(ret->thread_obj);
	ga_free(ret);
	return NULL;
}
void ga_thread_join(GaThread *thread) {
	pthread_join(thread->thread_obj->thread, 0);
}
void ga_thread_sleep(u32 ms) {
	usleep(ms * 1000);
}
void ga_thread_destroy(GaThread *thread) {
	pthread_cancel(thread->thread_obj->thread);
	pthread_join(thread->thread_obj->thread, NULL);
	pthread_attr_destroy(&thread->thread_obj->attr);
	ga_free(thread->thread_obj);
	ga_free(thread);
}

ga_result ga_mutex_create(GaMutex *res) {
	res->mutex = ga_alloc(sizeof(pthread_mutex_t));
	if (!res->mutex) return GA_ERR_SYS_LIB;
	if (pthread_mutex_init((pthread_mutex_t*)ret->mutex, NULL)) {
		ga_free(res->mutex);
		return GA_ERR_SYS_LIB;
	}
	return GA_OK;
}
void ga_mutex_destroy(GaMutex mutex) {
	if (!mutex.mutex) return;
	pthread_mutex_destroy((pthread_mutex_t*)mutex.mutex);
	ga_free(mutex.mutex);
}
void ga_mutex_lock(GaMutex mutex) {
	pthread_mutex_lock((pthread_mutex_t*)mutex.mutex);
}
void ga_mutex_unlock(GaMutex mutex) {
	pthread_mutex_unlock((pthread_mutex_t*)mutex.mutex);
}

#else
# error Threading primitives not yet implemented for this platform
#endif

/* System Functions */
static GaSystemOps default_callbacks = {
	.alloc = malloc,
	.realloc = realloc,
	.free = free,
};
static GaSystemOps *gaX_ops = &default_callbacks;

void *ga_alloc(usz size) {
	return gaX_ops->alloc(size);
}
void *ga_realloc(void *ptr, usz size) {
	return gaX_ops->realloc(ptr, size);
}
void ga_free(void *ptr) {
	gaX_ops->free(ptr);
}

ga_result ga_initialize_systemops(GaSystemOps *callbacks) {
	if (!callbacks || !callbacks->alloc || !callbacks->realloc || !callbacks->free) return GA_ERR_MIS_PARAM;
	*gaX_ops = *callbacks;
	return GA_OK;
}
ga_result ga_shutdown_systemops(void) {
	*gaX_ops = default_callbacks;
	return GA_OK;
}

static inline u16 ga_endian_bswap2(u16 x) {
	return (x<<8) | (x>>8);
}
static inline u32 ga_endian_bswap4(u32 x) {
	return ga_endian_bswap2(x >> 16)
	     | ga_endian_bswap2(x) << 16;
}

u16 ga_endian_tobe2(u16 x) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return ga_endian_bswap2(x);
#else
	return x;
#endif
}
u32 ga_endian_tobe4(u32 x) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return ga_endian_bswap4(x);
#else
	return x;
#endif
}
u16 ga_endian_tole2(u16 x) {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	return ga_endian_bswap2(x);
#else
	return x;
#endif
}
u32 ga_endian_tole4(u32 x) {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	return ga_endian_bswap4(x);
#else
	return x;
#endif
}

u16 ga_endian_frombe2(u16 x) {
	return ga_endian_tobe2(x);
}
u32 ga_endian_frombe4(u32 x) {
	return ga_endian_tobe4(x);
}
u16 ga_endian_fromle2(u16 x) {
	return ga_endian_tole2(x);
}
u32 ga_endian_fromle4(u32 x) {
	return ga_endian_tole4(x);
}
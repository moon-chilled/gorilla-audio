#define _POSIX_C_SOURCE 200809l //nanosleep
#include "gorilla/ga_internal.h"

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
	ResumeThread(ret->thread_obj->h);
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

ga_result ga_mutex_create(GaMutex *res) {
	res->mutex = ga_alloc(sizeof(CRITICAL_SECTION));
	InitializeCriticalSection((CRITICAL_SECTION*)res->mutex);
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
#include <time.h>

static s32 priority_lut[] = {
	[GaThreadPriority_Normal] = 0,
	[GaThreadPriority_Low] = 19,
	[GaThreadPriority_High] = -11,
	[GaThreadPriority_Highest] = -20
};

typedef struct { GaCbThreadFunc func; void *context; ga_result res; } ThreadWrapperContext;

struct GaThreadObj {
	pthread_t thread;
	pthread_attr_t attr;
	ThreadWrapperContext *ctx;
};

void *ga_thread_wrapper(void *context) { ThreadWrapperContext *ctx = context; ctx->res = ctx->func(ctx->context); return NULL; }

GaThread *ga_thread_create(GaCbThreadFunc thread_func, void *context,
                           GaThreadPriority priority, u32 stack_size) {
	struct sched_param param;
	GaThread *ret = ga_alloc(sizeof(GaThread));
	if (!ret) goto fail;
	GaThreadObj *thread_obj = ga_alloc(sizeof(GaThreadObj));
	if (!thread_obj) goto fail;
	thread_obj->ctx = ga_alloc(sizeof(ThreadWrapperContext));
	if (!thread_obj->ctx) goto fail;

	ret->thread_obj = thread_obj;
	ret->thread_func = thread_func;
	ret->context = context;
	ret->priority = priority;
	ret->stack_size = stack_size;

	thread_obj->ctx->func = thread_func;
	thread_obj->ctx->context = context;

	if (pthread_attr_init(&thread_obj->attr) != 0){} // report error
#if defined(__APPLE__) || defined(__ANDROID__) || defined(__FreeBSD__) || defined(__OpenBSD__)
	param.sched_priority = priority_lut[priority];
#elif defined(__linux__)
	param.__sched_priority = priority_lut[priority];
#endif
	if (pthread_attr_setschedparam(&thread_obj->attr, &param) != 0){} //report error
	if (pthread_attr_setstacksize(&thread_obj->attr, stack_size) != 0){} //report error
	if (pthread_create(&thread_obj->thread, &thread_obj->attr, ga_thread_wrapper, thread_obj->ctx) != 0) goto fail;

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
	nanosleep(&(struct timespec){.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000}, NULL);
}
void ga_thread_destroy(GaThread *thread) {
	pthread_cancel(thread->thread_obj->thread);
	pthread_join(thread->thread_obj->thread, NULL);
	pthread_attr_destroy(&thread->thread_obj->attr);
	ga_free(thread->thread_obj->ctx);
	ga_free(thread->thread_obj);
	ga_free(thread);
}

ga_result ga_mutex_create(GaMutex *res) {
	res->mutex = ga_alloc(sizeof(pthread_mutex_t));
	if (!res->mutex) return GA_ERR_SYS_LIB;
	if (pthread_mutex_init((pthread_mutex_t*)res->mutex, NULL)) {
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

static void *calloc_zalloc(usz sz) { return calloc(1, sz); }

/* System Functions */
static struct {
	void *(*alloc)(usz);
	void *(*zalloc)(usz);
	void *(*realloc)(void*,usz);
	void (*free)(void*);
} default_alloc_callbacks = {
	.alloc = malloc,
	.zalloc = calloc_zalloc,
	.realloc = realloc,
	.free = free,
}, alloc_callbacks = {
	.alloc = malloc,
	.zalloc = calloc_zalloc,
	.realloc = realloc,
	.free = free,
};

static void *alloc_zalloc(usz size) {
	void *ret = ga_alloc(size);
	if (!ret) return NULL;
	return memset(ret, 0, size);
}

void *ga_alloc(usz size) {
	return alloc_callbacks.alloc(size);
}
void *ga_zalloc(usz size) {
	return alloc_callbacks.zalloc(size);
}
void *ga_realloc(void *ptr, usz size) {
	return alloc_callbacks.realloc(ptr, size);
}
void ga_free(void *ptr) {
	alloc_callbacks.free(ptr);
}

ga_result ga_initialize_systemops(GaSystemOps *callbacks) {
	if (!callbacks || !callbacks->alloc || !callbacks->realloc || !callbacks->free) return GA_ERR_MIS_PARAM;
	alloc_callbacks.alloc = callbacks->alloc;
	alloc_callbacks.realloc = callbacks->realloc;
	alloc_callbacks.free = callbacks->free;
	alloc_callbacks.zalloc = alloc_callbacks.alloc == malloc ? calloc_zalloc : alloc_zalloc;
	return GA_OK;
}
ga_result ga_shutdown_systemops(void) {
	alloc_callbacks = default_alloc_callbacks;
	return GA_OK;
}

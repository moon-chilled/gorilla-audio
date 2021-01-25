// definitions that are shared between ga and gau, but still not meant to be externally visible
#ifndef _GORILLA_GA_U_INTERNAL_H
#define _GORILLA_GA_U_INTERNAL_H

#include <stdatomic.h>
#include <assert.h>

typedef ga_uint8  u8;
typedef ga_uint16 u16;
typedef ga_uint32 u32;
typedef ga_uint64 u64;

typedef ga_sint8  s8;
typedef ga_sint16 s16;
typedef ga_sint32 s32;
typedef ga_sint64 s64;

typedef ga_usize usz;
typedef ga_ssize ssz;

typedef ga_float32 f32;
typedef ga_float64 f64;

typedef ga_bool bool;
#define true ga_true
#define false ga_false

typedef struct {
	_Atomic u32 rc;
} RC; //refcount
typedef _Atomic bool atomic_bool;
typedef _Atomic u32 atomic_u32;
typedef ga_atomic_usize atomic_usz;
typedef _Atomic ssz atomic_ssz;

static inline ga_bool decref(RC *count) {
	_Atomic u32 old = atomic_fetch_add(&count->rc, -1);
	assert(old);
	return old == 1;
}
static inline void incref(RC *count) {
	atomic_fetch_add(&count->rc, 1);
}
static inline RC rc_new(void) {
	return (RC){1};
}

#define with_mutex(m) for (bool done = (ga_mutex_lock(m),false); !done; ga_mutex_unlock(m),done=true)

#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))

#endif //_GORILLA_GA_U_INTERNAL_H

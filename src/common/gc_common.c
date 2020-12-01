#include "gorilla/common/gc_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* System Functions */
static gc_SystemOps s_defaultCallbacks = {
	.allocFunc = malloc,
	.reallocFunc = realloc,
	.freeFunc = free,
};
gc_SystemOps* gcX_ops = &s_defaultCallbacks;

gc_result gc_initialize(gc_SystemOps* in_callbacks) {
	gcX_ops = in_callbacks ? in_callbacks : &s_defaultCallbacks;
	return GC_SUCCESS;
}
gc_result gc_shutdown() {
	gcX_ops = NULL;
	return GC_SUCCESS;
}

/* Circular Buffer Functions */
gc_CircBuffer* gc_buffer_create(gc_size in_size) {
	gc_CircBuffer* ret;
	if(!in_size || (in_size & (in_size - 1))) /* Must be power-of-two*/
		return 0;
	ret = gcX_ops->allocFunc(sizeof(gc_CircBuffer));
	ret->data = gcX_ops->allocFunc(in_size);
	ret->data_size = in_size;
	ret->next_avail = 0;
	ret->next_free = 0;
	return ret;
}
gc_result gc_buffer_destroy(gc_CircBuffer *b) {
	gcX_ops->freeFunc(b->data);
	gcX_ops->freeFunc(b);
	return GC_SUCCESS;
}
gc_size gc_buffer_bytesAvail(gc_CircBuffer *b) {
	/* producer/consumer call (all race-induced errors should be tolerable) */
	return b->next_free - b->next_avail;
}
gc_size gc_buffer_bytesFree(gc_CircBuffer *b) {
	/* producer/consumer call (all race-induced errors should be tolerable) */
	return b->data_size - gc_buffer_bytesAvail(b);
}
gc_int32 gc_buffer_getFree(gc_CircBuffer *b, gc_size num_bytes,
                           void **data1, gc_size* size1,
			   void **data2, gc_size* size2) {
	/* producer-only call */
	gc_size size = b->data_size;
	gc_size next_free = b->next_free % size;
	gc_size maxBytes = size - next_free;
	if(num_bytes > gc_buffer_bytesFree(b)) return -1;
	if(maxBytes >= num_bytes) {
		*data1 = &b->data[next_free];
		*size1 = num_bytes;
		return 1;
	} else {
		*data1 = &b->data[next_free];
		*size1 = maxBytes;
		*data2 = &b->data[0];
		*size2 = num_bytes - maxBytes;
		return 2;
	}
}
gc_result gc_buffer_write(gc_CircBuffer *b, void *in_data,
                          gc_size num_bytes) {
	/* TODO: Make this call gc_buffer_getFree() instead of duping code */

	/* producer-only call */
	gc_size size = b->data_size;
	gc_size next_free = b->next_free % size;
	gc_size maxBytes = size - next_free;
	if (num_bytes > gc_buffer_bytesFree(b)) return GC_ERROR_GENERIC;
	if (maxBytes >= num_bytes) {
		memcpy(&b->data[next_free], in_data, num_bytes);
	} else {
		memcpy(&b->data[next_free], in_data, maxBytes);
		memcpy(&b->data[0], (char*)in_data + maxBytes, num_bytes - maxBytes);
	}
	b->next_free += num_bytes;
	return GC_SUCCESS;
}

gc_int32 gc_buffer_getAvail(gc_CircBuffer *b, gc_size num_bytes,
                            void** data1, gc_size* size1,
			    void** data2, gc_size* size2) {
	/* consumer-only call */
	gc_size bytesAvailable = gc_buffer_bytesAvail(b);
	gc_size size = b->data_size;
	gc_size next_avail = b->next_avail % size;
	gc_size maxBytes = size - next_avail;
	if (bytesAvailable < num_bytes) return -1;
	if (maxBytes >= num_bytes) {
		*data1 = &b->data[next_avail];
		*size1 = num_bytes;
		*data2 = 0;
		*size2 = 0;
		return 1;
	} else {
		*data1 = &b->data[next_avail];
		*size1 = maxBytes;
		*data2 = &b->data[0];
		*size2 = num_bytes - maxBytes;
		return 2;
	}
}
void gc_buffer_read(gc_CircBuffer *b, void *dst, gc_size num_bytes) {
	/* consumer-only call */
	void* data[2];
	gc_size size[2];
	gc_int32 ret = gc_buffer_getAvail(b, num_bytes,
			&data[0], &size[0],
			&data[1], &size[1]);
	if (ret >= 1) {
		memcpy(dst, data[0], size[0]);
		if (ret == 2)
			memcpy((char*)dst + size[0], data[1], size[1]);
	}
}
void gc_buffer_produce(gc_CircBuffer *b, gc_size num_bytes) {
	/* producer-only call */
	b->next_free += num_bytes;
}

void gc_buffer_consume(gc_CircBuffer *b, gc_size num_bytes) {
	/* consumer-only call */
	b->next_avail += num_bytes;
}

/* List Functions */
void gc_list_head(gc_Link* in_head) {
	in_head->next = in_head;
	in_head->prev = in_head;
}
void gc_list_link(gc_Link* in_head, gc_Link* in_link, void* in_data) {
	in_link->data = in_data;
	in_link->prev = in_head;
	in_link->next = in_head->next;
	in_head->next->prev = in_link;
	in_head->next = in_link;
}
void gc_list_unlink(gc_Link* in_link) {
	in_link->prev->next = in_link->next;
	in_link->next->prev = in_link->prev;
	in_link->prev = 0;
	in_link->next = 0;
	in_link->data = 0;
}

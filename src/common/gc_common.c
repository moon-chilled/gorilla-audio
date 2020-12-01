#include "gorilla/common/ga_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* System Functions */
static GaSystemOps s_defaultCallbacks = {
	.allocFunc = malloc,
	.reallocFunc = realloc,
	.freeFunc = free,
};
GaSystemOps* gcX_ops = &s_defaultCallbacks;

ga_result ga_initialize_systemops(GaSystemOps* in_callbacks) {
	gcX_ops = in_callbacks ? in_callbacks : &s_defaultCallbacks;
	return GA_OK;
}
ga_result ga_shutdown_systemops() {
	gcX_ops = NULL;
	return GA_OK;
}

/* Circular Buffer Functions */
GaCircBuffer* ga_buffer_create(gc_size in_size) {
	GaCircBuffer* ret;
	if(!in_size || (in_size & (in_size - 1))) /* Must be power-of-two*/
		return 0;
	ret = gcX_ops->allocFunc(sizeof(GaCircBuffer));
	ret->data = gcX_ops->allocFunc(in_size);
	ret->data_size = in_size;
	ret->next_avail = 0;
	ret->next_free = 0;
	return ret;
}
ga_result ga_buffer_destroy(GaCircBuffer *b) {
	gcX_ops->freeFunc(b->data);
	gcX_ops->freeFunc(b);
	return GA_OK;
}
gc_size ga_buffer_bytesAvail(GaCircBuffer *b) {
	/* producer/consumer call (all race-induced errors should be tolerable) */
	return b->next_free - b->next_avail;
}
gc_size ga_buffer_bytesFree(GaCircBuffer *b) {
	/* producer/consumer call (all race-induced errors should be tolerable) */
	return b->data_size - ga_buffer_bytesAvail(b);
}
gc_uint8 ga_buffer_getFree(GaCircBuffer *b, gc_size num_bytes,
                           void **data1, gc_size* size1,
			   void **data2, gc_size* size2) {
	/* producer-only call */
	gc_size size = b->data_size;
	gc_size next_free = b->next_free % size;
	gc_size maxBytes = size - next_free;
	if(num_bytes > ga_buffer_bytesFree(b)) return 0;
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
ga_result ga_buffer_write(GaCircBuffer *b, void *data, gc_size num_bytes) {
	/* TODO: Make this call ga_buffer_getFree() instead of duping code */

	/* producer-only call */
	gc_size size = b->data_size;
	gc_size next_free = b->next_free % size;
	gc_size maxBytes = size - next_free;
	if (num_bytes > ga_buffer_bytesFree(b)) return GA_ERR_GENERIC;
	if (maxBytes >= num_bytes) {
		memcpy(&b->data[next_free], data, num_bytes);
	} else {
		memcpy(&b->data[next_free], data, maxBytes);
		memcpy(&b->data[0], (char*)data + maxBytes, num_bytes - maxBytes);
	}
	b->next_free += num_bytes;
	return GA_OK;
}

gc_uint8 ga_buffer_getAvail(GaCircBuffer *b, gc_size num_bytes,
                            void **data1, gc_size* size1,
			    void **data2, gc_size* size2) {
	/* consumer-only call */
	gc_size bytesAvailable = ga_buffer_bytesAvail(b);
	gc_size size = b->data_size;
	gc_size next_avail = b->next_avail % size;
	gc_size maxBytes = size - next_avail;
	if (bytesAvailable < num_bytes) return 0;
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
void ga_buffer_read(GaCircBuffer *b, void *dst, gc_size num_bytes) {
	/* consumer-only call */
	void* data[2];
	gc_size size[2];
	gc_uint8 nbuf = ga_buffer_getAvail(b, num_bytes,
			&data[0], &size[0],
			&data[1], &size[1]);
	if (nbuf >= 1) memcpy(dst, data[0], size[0]);
	if (nbuf >= 2) memcpy((char*)dst + size[0], data[1], size[1]);
}
void ga_buffer_produce(GaCircBuffer *b, gc_size num_bytes) {
	/* producer-only call */
	b->next_free += num_bytes;
}

void ga_buffer_consume(GaCircBuffer *b, gc_size num_bytes) {
	/* consumer-only call */
	b->next_avail += num_bytes;
}

/* List Functions */
void ga_list_head(GaLink* in_head) {
	in_head->next = in_head;
	in_head->prev = in_head;
}
void ga_list_link(GaLink* in_head, GaLink* in_link, void* in_data) {
	in_link->data = in_data;
	in_link->prev = in_head;
	in_link->next = in_head->next;
	in_head->next->prev = in_link;
	in_head->next = in_link;
}
void ga_list_unlink(GaLink* in_link) {
	in_link->prev->next = in_link->next;
	in_link->next->prev = in_link->prev;
	in_link->prev = 0;
	in_link->next = 0;
	in_link->data = 0;
}

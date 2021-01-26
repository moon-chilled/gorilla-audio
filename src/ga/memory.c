#include "gorilla/ga_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Circular Buffer Functions */
GaCircBuffer *ga_buffer_create(usz size) {
	GaCircBuffer *ret;
	if(!size || (size & (size - 1))) /* Must be power-of-two*/
		return NULL;
	ret = ga_alloc(sizeof(GaCircBuffer));
	ret->data = ga_alloc(size);
	ret->data_size = size;
	ret->next_avail = 0;
	ret->next_free = 0;
	return ret;
}
ga_result ga_buffer_destroy(GaCircBuffer *b) {
	ga_free(b->data);
	ga_free(b);
	return GA_OK;
}
usz ga_buffer_bytes_avail(GaCircBuffer *b) {
	/* producer/consumer call (all race-induced errors should be tolerable) */
	return b->next_free - b->next_avail;
}
usz ga_buffer_bytes_free(GaCircBuffer *b) {
	/* producer/consumer call (all race-induced errors should be tolerable) */
	return b->data_size - ga_buffer_bytes_avail(b);
}
u8 ga_buffer_get_free(GaCircBuffer *b, usz num_bytes,
                           void **data1, usz *size1,
			   void **data2, usz *size2) {
	/* producer-only call */
	usz size = b->data_size;
	usz next_free = b->next_free % size;
	usz max_bytes = size - next_free;
	if (num_bytes > ga_buffer_bytes_free(b)) return 0;
	if (max_bytes >= num_bytes) {
		*data1 = &b->data[next_free];
		*size1 = num_bytes;
		return 1;
	} else {
		*data1 = &b->data[next_free];
		*size1 = max_bytes;
		*data2 = &b->data[0];
		*size2 = num_bytes - max_bytes;
		return 2;
	}
}
ga_result ga_buffer_write(GaCircBuffer *b, void *data, usz num_bytes) {
	/* TODO: Make this call ga_buffer_get_free() instead of duping code */

	/* producer-only call */
	usz size = b->data_size;
	usz next_free = b->next_free % size;
	usz maxBytes = size - next_free;
	if (num_bytes > ga_buffer_bytes_free(b)) return GA_ERR_MIS_PARAM;
	if (maxBytes >= num_bytes) {
		memcpy(&b->data[next_free], data, num_bytes);
	} else {
		memcpy(&b->data[next_free], data, maxBytes);
		memcpy(&b->data[0], (char*)data + maxBytes, num_bytes - maxBytes);
	}
	b->next_free += num_bytes;
	return GA_OK;
}

u8 ga_buffer_get_avail(GaCircBuffer *b, usz num_bytes,
                             void **data1, usz *size1,
			     void **data2, usz *size2) {
	/* consumer-only call */
	usz bytesAvailable = ga_buffer_bytes_avail(b);
	usz size = b->data_size;
	usz next_avail = b->next_avail % size;
	usz maxBytes = size - next_avail;
	if (bytesAvailable < num_bytes) {
		*data1 = *data2 = NULL;
		*size1 = *size2 = 0;
		return 0;
	}
	if (maxBytes >= num_bytes) {
		*data1 = &b->data[next_avail];
		*size1 = num_bytes;
		*data2 = NULL;
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
void ga_buffer_read(GaCircBuffer *b, void *dst, usz num_bytes) {
	/* consumer-only call */
	void *data[2];
	usz size[2];
	u8 nbuf = ga_buffer_get_avail(b, num_bytes,
	                                 &data[0], &size[0],
	                                 &data[1], &size[1]);
	if (nbuf >= 1) memcpy(dst, data[0], size[0]);
	if (nbuf >= 2) memcpy((char*)dst + size[0], data[1], size[1]);
}
void ga_buffer_produce(GaCircBuffer *b, usz num_bytes) {
	/* producer-only call */
	b->next_free += num_bytes;
}

void ga_buffer_consume(GaCircBuffer *b, usz num_bytes) {
	/* consumer-only call */
	b->next_avail += num_bytes;
}

/* List Functions */
void ga_list_head(GaLink *head) {
	head->next = head;
	head->prev = head;
}
void ga_list_link(GaLink *head, GaLink *link, void *data) {
	link->data = data;
	link->prev = head;
	link->next = head->next;
	head->next->prev = link;
	head->next = link;
}
void ga_list_unlink(GaLink *link) {
	link->prev->next = link->next;
	link->next->prev = link->prev;
	link->prev = NULL;
	link->next = NULL;
	link->data = NULL;
}


#include "lf_queue.h"

#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>

typedef uint64_t element_descriptor_t;

#define LF_QUEUE_MAGIC 0x68478320ac2
struct lf_queue {
    uint64_t magic;
    size_t n_elements;
    size_t element_size;
    element_descriptor_t free_head;
    size_t head;
    size_t tail;
    uint32_t mod_count;
    bool should_free;
};

#define USED_BIT                0x8000000000000000
#define ELEMENT_OFFSET_MASK     0x00000000ffffffff
#define QUEUE_GEN_MASK          0x7fffffff00000000

static inline size_t get_min_element_size(size_t element_size)
{
	if (element_size < sizeof(element_descriptor_t)) {
		// when the element is in the free list we use the element data
		// to store the element_descriptor of the next element in the
		// free list
		return sizeof(element_descriptor_t);
	}
	return element_size;
}

static inline size_t raw_elem_size(size_t element_size)
{
	return get_min_element_size(element_size);
}

static inline void *elements_start(lf_queue *queue)
{
	return ((void*)queue + sizeof(*queue));
}

static inline size_t data_offset(lf_queue *queue, void *element_data)
{
	return ((element_data - elements_start(queue)) / raw_elem_size(queue->element_size));
}

static inline void *get_elem_data_by_offset(lf_queue *queue, size_t element_offset)
{
	return ((void*)queue + sizeof(*queue)) + (element_offset * raw_elem_size(queue->element_size));

}

static inline size_t get_element_descriptor(lf_queue *queue, void *element_data)
{
	size_t queue_gen = (queue->tail + 1) / queue->n_elements;
	size_t element_descriptor = queue_gen;
	element_descriptor = (element_descriptor << 32) & QUEUE_GEN_MASK;
	return element_descriptor | data_offset(queue, element_data);
}

static inline void *get_element_data_by_descriptor(lf_queue *queue, size_t element_descriptor)
{
	return get_elem_data_by_offset(queue, element_descriptor & ELEMENT_OFFSET_MASK);
}

static inline element_descriptor_t get_free_list_descriptor(lf_queue *queue, void *edata)
{
	size_t mod_count = queue->mod_count;
	size_t element_descriptor = mod_count << 32;
	return element_descriptor | data_offset(queue, edata);
}

static inline element_descriptor_t *descriptors_start(lf_queue *queue)
{
	return elements_start(queue) + (queue->n_elements * raw_elem_size(queue->element_size));
}

size_t lf_queue_get_required_memory(size_t n_elements, size_t element_size)
{
	return sizeof(lf_queue) + (n_elements * raw_elem_size(element_size))
		+ (n_elements * sizeof(element_descriptor_t));
}

lf_queue *lf_queue_init(size_t n_elements, size_t element_size)
{
	void *buff;
	lf_queue *queue;

	if (n_elements > 0xffffffff) {
		// max queue size is limited to the max offset value in the
		// element_descriptor which is 2^32
		return NULL;
	}
	element_size = get_min_element_size(element_size);

	buff = malloc(lf_queue_get_required_memory(n_elements, element_size));
	if (!buff) {
		return NULL;
	}
	queue = lf_queue_mem_init(buff, n_elements, element_size);
	if (queue == NULL) {
		free(buff);
		return NULL;
	}
	queue->should_free = true;
	return queue;
}

lf_queue *lf_queue_mem_init(void *mem, size_t n_elements, size_t element_size)
{
	size_t i;
	void *curr;
	void *next;
	element_descriptor_t *element_descriptors;
	lf_queue *queue = mem;

	if (n_elements == 0 || element_size == 0) {
		return NULL;
	}
	element_size = get_min_element_size(element_size);

	queue->magic = LF_QUEUE_MAGIC;
	queue->n_elements = n_elements;
	queue->element_size = element_size;
	element_descriptors = descriptors_start(queue);
	memset(element_descriptors, 0, n_elements * sizeof(element_descriptor_t));
	queue->head = 1;
	queue->tail = 0;
	queue->should_free = false;
	queue->mod_count = 1;

	curr = elements_start(queue);
	for (i = 0; i < n_elements; ++i) {
		next = curr + raw_elem_size(element_size);
		if (i == n_elements - 1) {
			*(element_descriptor_t *)curr = 0;
		} else {
			*(element_descriptor_t *)curr = get_free_list_descriptor(
				queue, next);
		}
		curr = next;
	}

	queue->free_head = get_free_list_descriptor(queue, elements_start(queue));
	return queue;
}

lf_queue *lf_queue_attach(void *mem)
{
	lf_queue *queue = mem;
	if (queue->magic != LF_QUEUE_MAGIC) {
		return NULL;
	}
	return queue;
}

void lf_queue_destroy(lf_queue *queue)
{
	if (queue->should_free) {
		free(queue);
	}
}

void *lf_queue_get(lf_queue *queue)
{
	element_descriptor_t curr_free = 0;
	element_descriptor_t prev_val;

	assert(queue->magic == LF_QUEUE_MAGIC);
	curr_free = queue->free_head;
	while (curr_free != 0) {
		void *data = get_element_data_by_descriptor(queue, curr_free);
		element_descriptor_t next_desc = *(element_descriptor_t*)data;
		prev_val = __sync_val_compare_and_swap(&queue->free_head,
		                                       curr_free, next_desc);
		if (prev_val == curr_free) {
			// swap was successful
			__sync_add_and_fetch(&queue->mod_count, 1);
			 return data;
		}
		curr_free = prev_val;
	}
	return NULL;
}

void lf_queue_put(lf_queue *queue, void *data)
{
	element_descriptor_t curr_free = queue->free_head;
	element_descriptor_t prev_val;

	assert(queue->magic == LF_QUEUE_MAGIC);
	do {
		*(element_descriptor_t *)data = curr_free;
		prev_val = __sync_val_compare_and_swap(&queue->free_head,
		                                       curr_free,
		                                       get_free_list_descriptor(queue, data));
		if (prev_val == curr_free) {
			break;
		}
		curr_free = prev_val;
	} while (true);
}

void lf_queue_enqueue(lf_queue *queue, void *data)
{
	element_descriptor_t *desc_ptr;
	element_descriptor_t elem_desc;

	assert(queue->magic == LF_QUEUE_MAGIC);
	do {
		size_t tail = __sync_add_and_fetch(&queue->tail, 0);
		desc_ptr = &descriptors_start(queue)[(tail + 1) % queue->n_elements];
		elem_desc = *desc_ptr;
		if ((elem_desc & USED_BIT)) {
			// this slot is already in use, some other thread won the
			// race to enqueue it
			continue;
		}
		if ((elem_desc & ~USED_BIT) > tail) {
			// this slot is free, but the value this thread think tail
			// has is outdated meaning that some other thread(s) have
			// already enqueued and dequeued this slot since we got
			// the value of tail
			continue;
		}
		if (__sync_bool_compare_and_swap(desc_ptr, elem_desc,
		                                 USED_BIT | get_element_descriptor(queue, data))) {
			// one could claim since tail always advances there is the
			// risk of a wraparound. However this is not a practical
			// concern since even if you add a new item to the queue
			// every nano sec it would still take ~584 years for 64 bits
			// to wrap.
			__sync_add_and_fetch(&queue->tail, 1);
			return;
		}
	} while (true);
}

void *lf_queue_dequeue(lf_queue *queue)
{
	element_descriptor_t elem_desc;
	element_descriptor_t *desc_ptr;

	assert(queue->magic == LF_QUEUE_MAGIC);
	do {
		size_t head = __sync_add_and_fetch(&queue->head, 0);
		size_t tail = __sync_add_and_fetch(&queue->tail, 0);
		if (head > tail) {
			return NULL;
		}
		desc_ptr = &descriptors_start(queue)[head % queue->n_elements];
		elem_desc = *desc_ptr;
		if ((elem_desc & USED_BIT) == 0) {
			// some other thread have already dequeued this element
			continue;
		}
		int64_t desc_queue_gen = (int64_t) ((elem_desc & QUEUE_GEN_MASK) >> 32);
		int64_t current_queue_gen = (int64_t) ((head / queue->n_elements) & 0xffffffff);
		if (desc_queue_gen - current_queue_gen > 0 && desc_queue_gen - current_queue_gen < 0x0fffff) {
			// The queue gen represent the number of times the respective pointer (head or tail) has wrapped
			// around the descriptors array. A free slot in the descriptor array contains the value of the
			// tail queue gen at the time this slot was freed. current_queue_gen contains the queue gen of
			// what this thread thinks is the current value of head.
			// If the above condition applies it means that value of head this thread has is outdated
			// in relation to the element it is looking at
			continue;
		}
		if (__sync_bool_compare_and_swap(desc_ptr, elem_desc, tail)) {
			__sync_add_and_fetch(&queue->head, 1);
			return get_element_data_by_descriptor(queue, elem_desc & ~USED_BIT);
		}
	} while (true);
}

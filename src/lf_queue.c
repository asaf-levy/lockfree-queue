#include "lf_queue.h"

#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <syscall.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

typedef struct lf_element_impl lf_element_impl_t;
typedef uint64_t element_descriptor_t;

typedef struct __attribute__((packed)) lf_element_impl {
    lf_element_t elem;
    // followed by the element data
} lf_element_impl_t;

#define LF_QUEUE_MAGIC 0x68478320ac2
typedef struct lf_queue_impl {
    uint64_t magic;
    size_t n_elements;
    size_t element_size;
    element_descriptor_t free_head;
    size_t head;
    size_t tail;
    uint32_t mod_count;
    bool should_free;
} lf_queue_impl_t;


#ifdef QDEBUG
__thread pid_t g_tid = 0;
pid_t get_tid(void)
{
	if (!g_tid) {
		g_tid = (pid_t)syscall(SYS_gettid);
	}
	return g_tid;
}

#define PRINT(FMT, ARGS...)  \
	printf(FMT, ##ARGS)
#else
#define PRINT(FMT, ARGS...)
#endif

#define container_of(ptr, container, member) \
	((container *)((void *)ptr - offsetof(container, member)))
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
	element_size = get_min_element_size(element_size);
	return element_size + sizeof(lf_element_impl_t);
}

static inline lf_element_impl_t * elements_start(lf_queue_impl_t *queue)
{
	return ((void*)queue + sizeof(*queue));
}

static inline size_t elem_offset(lf_queue_impl_t *queue, lf_element_impl_t *element)
{
	return (((void*)element - (void*)elements_start(queue)) / raw_elem_size(queue->element_size));
}

static inline lf_element_impl_t *get_elem_by_offset(lf_queue_impl_t *queue,
                                                    size_t element_offset)
{
	lf_element_impl_t *e = ((void*)queue + sizeof(*queue)) +
		(element_offset * raw_elem_size(queue->element_size));
	e->elem.data = (void*)&e->elem + sizeof(e->elem);
	return e;

}

static inline size_t get_element_descriptor(lf_queue_impl_t *queue,
                                            lf_element_impl_t *element)
{
	size_t queue_gen = (queue->tail + 1) / queue->n_elements;
	size_t element_descriptor = queue_gen;
	element_descriptor = (element_descriptor << 32) & QUEUE_GEN_MASK;
	return element_descriptor | elem_offset(queue, element);
}

static inline lf_element_impl_t *get_element_by_descriptor(lf_queue_impl_t *queue,
                                                           size_t element_descriptor)
{
	return get_elem_by_offset(queue, element_descriptor & ELEMENT_OFFSET_MASK);
}

static inline element_descriptor_t get_free_list_descriptor(lf_queue_impl_t *queue,
                                                            lf_element_impl_t *element)
{
	size_t mod_count = queue->mod_count;
	size_t element_descriptor = mod_count << 32;
	return element_descriptor | elem_offset(queue, element);
}

static inline element_descriptor_t *descriptors_start(lf_queue_impl_t *queue)
{
	return (void*)elements_start(queue) +
		(queue->n_elements * raw_elem_size(queue->element_size));
}

size_t lf_queue_get_required_memory(size_t n_elements, size_t element_size)
{
	return sizeof(lf_queue_impl_t) + (n_elements * raw_elem_size(element_size))
		+ (n_elements * sizeof(element_descriptor_t));
}

int lf_queue_init(lf_queue_handle_t *queue, size_t n_elements, size_t element_size)
{
	int err;
	lf_queue_impl_t *qimpl;
	void *buff;

	if (n_elements > 0xffffffff) {
		// max queue size is limited to the max offset value in the
		// element_descriptor which is 2^32
		return EINVAL;
	}
	element_size = get_min_element_size(element_size);

	buff = malloc(lf_queue_get_required_memory(n_elements, element_size));
	if (!buff) {
		return ENOMEM;
	}
	err = lf_queue_mem_init(queue, buff, n_elements, element_size);
	if (err) {
		free(buff);
	}
	qimpl = (lf_queue_impl_t *)queue->handle;
	qimpl->should_free = true;

	return err;
}

int lf_queue_mem_init(lf_queue_handle_t *queue, void *mem, size_t n_elements,
                      size_t element_size)
{
	size_t i;
	lf_element_impl_t *curr;
	lf_element_impl_t *next;
	element_descriptor_t *element_descriptors;
	lf_queue_impl_t *qimpl = mem;

	if (n_elements == 0 || element_size == 0 || queue == NULL) {
		return EINVAL;
	}
	element_size = get_min_element_size(element_size);

	qimpl->magic = LF_QUEUE_MAGIC;
	qimpl->n_elements = n_elements;
	qimpl->element_size = element_size;
	element_descriptors = descriptors_start(qimpl);
	memset(element_descriptors, 0, n_elements * sizeof(element_descriptor_t));
	qimpl->head = 1;
	qimpl->tail = 0;
	qimpl->should_free = false;
	qimpl->mod_count = 1;

	curr = elements_start(qimpl);
	for (i = 0; i < n_elements; ++i) {
		next = (lf_element_impl_t *)((void *)curr + raw_elem_size(element_size));
		curr->elem.data = (void*)&curr->elem + sizeof(curr->elem);
		if (i == n_elements - 1) {
			*(element_descriptor_t *)curr->elem.data = 0;
		} else {
			*(element_descriptor_t *) curr->elem.data = get_free_list_descriptor(
				qimpl, next);
		}
		curr = next;
	}

	qimpl->free_head = get_free_list_descriptor(qimpl, elements_start(qimpl));
	queue->handle = qimpl;
	return 0;
}

int lf_queue_attach(lf_queue_handle_t *queue, void *mem)
{
	lf_queue_impl_t *qimpl = mem;
	if (qimpl->magic != LF_QUEUE_MAGIC) {
		return EINVAL;
	}
	queue->handle = mem;
	return 0;
}

void lf_queue_destroy(lf_queue_handle_t queue)
{
	lf_queue_impl_t *qimpl = queue.handle;
	if (qimpl->should_free) {
		free(qimpl);
	}
}

int lf_queue_get(lf_queue_handle_t queue, lf_element_t **element)
{
	lf_queue_impl_t *qimpl = queue.handle;
	element_descriptor_t curr_free = 0;
	element_descriptor_t prev_val;

	assert(qimpl->magic == LF_QUEUE_MAGIC);
	curr_free = qimpl->free_head;
	while (curr_free != 0) {
		lf_element_impl_t *eimpl = get_element_by_descriptor(qimpl, curr_free);
		element_descriptor_t next_desc = *(element_descriptor_t*)eimpl->elem.data;
		prev_val = __sync_val_compare_and_swap(&qimpl->free_head,
		                                       curr_free, next_desc);
		if (prev_val == curr_free) {
			// swap was successful
			__sync_add_and_fetch(&qimpl->mod_count, 1);
			*element = &eimpl->elem;
			return 0;
		}
		curr_free = prev_val;
	}
	return ENOMEM;
}

void lf_queue_put(lf_queue_handle_t queue, lf_element_t *element)
{
	lf_queue_impl_t *qimpl = queue.handle;
	lf_element_impl_t *element_impl = container_of(element, lf_element_impl_t, elem);
	element_descriptor_t curr_free = qimpl->free_head;
	element_descriptor_t prev_val;

	assert(qimpl->magic == LF_QUEUE_MAGIC);
	do {
		*(element_descriptor_t *)element_impl->elem.data = curr_free;
		prev_val = __sync_val_compare_and_swap(&qimpl->free_head,
		                                       curr_free, get_free_list_descriptor(qimpl, element_impl));
		if (prev_val == curr_free) {
//			PRINT("(%d) PUT curr_free=%p element=%p\n", get_tid(), curr_free, element);
			break;
		}
		curr_free = prev_val;
	} while (true);
}

void lf_queue_enqueue(lf_queue_handle_t queue, lf_element_t *element)
{
	lf_queue_impl_t *qimpl = queue.handle;
	lf_element_impl_t *e_impl = container_of(element, lf_element_impl_t, elem);
#ifdef QDEBUG
	size_t prev_head;
	size_t prev_tail;
#endif
	size_t tail;
	element_descriptor_t *desc_ptr;
	element_descriptor_t elem_desc;

	assert(qimpl->magic == LF_QUEUE_MAGIC);
	do {
		tail = __sync_add_and_fetch(&qimpl->tail, 0);
//		prev_tail = tail;
		desc_ptr = &descriptors_start(qimpl)[(tail + 1) % qimpl->n_elements];
		elem_desc = *desc_ptr;
//				prev_head = qimpl->head;
//		PRINT("(%d) ENQ 1 desc_ptr=%p element_descriptor=%lx head=%lu tail=%lu\n", get_tid(),
//		       desc_ptr, elem_desc, qimpl->head, tail);
		if ((elem_desc & USED_BIT)) {
			// this slot is already in use, some other thread won the
			// race to enqueue it
			PRINT("(%d) ENQ SLOT-TAKEN desc_ptr=%p elem_desc=%lx head=%lu tail=%lu\n", get_tid(),
			      desc_ptr, elem_desc, qimpl->head, tail);
			continue;
		}
		if ((elem_desc & ~USED_BIT) && ((elem_desc & ~USED_BIT) > tail)) {
			// this slot is free, but the value this thread think tail
			// has is outdated meaning that some other thread(s) have
			// already enqueued and dequeued this slot since we got
			// the value of tail
			PRINT("(%d) ENQ TAIL-UPDATED desc_ptr=%p desc_tail=%lu head=%lu tail=%lu\n", get_tid(),
			      desc_ptr, elem_desc & ~USED_BIT, qimpl->head, tail);
			continue;
		}
		if (__sync_bool_compare_and_swap(desc_ptr, elem_desc,
		                                 USED_BIT | get_element_descriptor(qimpl, e_impl))) {
			// one could claim since tail always advances there is the
			// risk of a wraparound. However this is not a practical
			// concern since even if you add a new item to the queue
			// every nano sec it would still take ~584 years for 64 bits
			// to wrap.
			tail = __sync_add_and_fetch(&qimpl->tail, 1);
			PRINT("(%d) ENQ 2 desc_ptr=%p element_descriptor=%lu calc_element_descriptor=%lx prev_head=%lu head=%lu prev_tail=%lu tail=%lu\n", get_tid(),
			       desc_ptr, elem_desc,
			       get_element_descriptor(qimpl, e_impl), prev_head, qimpl->head, prev_tail, tail);
			return;
		} else {
			PRINT("(%d) ENQ RETRY desc_ptr=%p head=%lu tail=%lu\n", get_tid(),
			      desc_ptr, qimpl->head, tail);
		}
	} while (true);
}

int lf_queue_dequeue(lf_queue_handle_t queue, lf_element_t **element)
{
	lf_queue_impl_t *qimpl = queue.handle;
	size_t head;
#ifdef QDEBUG
	size_t prev_head;
#endif
	size_t tail;
	element_descriptor_t elem_desc;
	int64_t desc_queue_gen;
	int64_t current_queue_gen;
	element_descriptor_t *desc_ptr;

	assert(qimpl->magic == LF_QUEUE_MAGIC);
	do {
		head = __sync_add_and_fetch(&qimpl->head, 0);
#ifdef QDEBUG
		prev_head = head;
#endif
		tail = __sync_add_and_fetch(&qimpl->tail, 0);
		if (head > tail) {
			return ENOMEM;
		}
		desc_ptr = &descriptors_start(qimpl)[head % qimpl->n_elements];
		elem_desc = *desc_ptr;
		desc_queue_gen = (int64_t) ((elem_desc & QUEUE_GEN_MASK) >> 32);
		current_queue_gen = (int64_t) ((head / qimpl->n_elements) & 0xffffffff);
		if (desc_queue_gen - current_queue_gen > 0 && desc_queue_gen - current_queue_gen < 0x0fffff) {
			PRINT("(%d) DEQ OUTDATED QUEUE GEN, desc_queue_gen=%lu current_queue_gen=%lu element_descriptor=0x%lx head=%lu tail=%lu\n", get_tid(),
			      desc_queue_gen, current_queue_gen, elem_desc, head, tail);
			// some other thread have already dequeued this element
			continue;
		}
//		PRINT("(%d) DEQ 1 element_descriptor=%lx head=%lu tail=%lu\n", get_tid(),
//		       elem_desc, head, tail);
		if ((elem_desc & USED_BIT) == 0) {
			PRINT("(%d) DEQ USED_BIT NOT SET, element_descriptor=%lu head=%lu tail=%lu\n", get_tid(),
			       elem_desc, head, tail);
			// some other thread have already dequeued this element
			continue;
		}
		if (__sync_bool_compare_and_swap(desc_ptr, elem_desc, tail)) {
			__sync_add_and_fetch(&qimpl->head, 1);
			*element = &get_element_by_descriptor(qimpl, elem_desc & ~USED_BIT)->elem;
			PRINT("(%d) DEQ 2 *element=%p element_descriptor=%lx desc_queue_gen=%lu prev_head=%lu head=%lu tail=%lu\n",
			      get_tid(), *element, elem_desc, desc_queue_gen, prev_head, head, tail);
			return 0;
		} else {
			PRINT("(%d) DEQ RETRY element_descriptor=%lx head=%lu tail=%lu\n", get_tid(),
			       elem_desc, head, tail);
		}
	} while (true);
}


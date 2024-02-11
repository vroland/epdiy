#include <string.h>
#include <esp_attr.h>
#include <stdlib.h>
#include <assert.h>
#include <esp_heap_caps.h>

#include "line_queue.h"
#include "render_method.h"

static inline int ceil_div(int x, int y) { return x / y + (x % y != 0); }

/// Initialize the line queue and allocate memory.
LineQueue_t lq_init(int queue_len, int element_size, bool use_mask) {
    LineQueue_t queue;
    queue.element_size = element_size;
    queue.size = queue_len;
    queue.current = 0;
    queue.last = 0;

    int elem_buf_size = ceil_div(element_size, 16) * 16;

    queue.bufs = calloc(queue.size, elem_buf_size);
    assert(queue.bufs != NULL);

    for (int i=0; i<queue.size; i++) {
        queue.bufs[i] = heap_caps_aligned_alloc(16, elem_buf_size, MALLOC_CAP_INTERNAL);
        assert(queue.bufs[i] != NULL);
    }

    if (use_mask) {
        queue.mask_buffer_len = elem_buf_size;
        queue.mask_buffer = heap_caps_aligned_alloc(16, elem_buf_size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        assert(queue.mask_buffer != NULL);
    } else {
        queue.mask_buffer_len = 0;
        queue.mask_buffer = NULL;
    }

    return queue;
}

/// Deinitialize the line queue and free memory.
void lq_free(LineQueue_t* queue) {
    for (int i=0; i<queue->size; i++) {
        heap_caps_free(queue->bufs[i]);
    }

    if (queue->mask_buffer != NULL) {
        heap_caps_free(queue->mask_buffer);
    }
    free(queue->bufs);
}

uint8_t* IRAM_ATTR lq_current(LineQueue_t* queue) {
    int current = atomic_load_explicit(&queue->current, memory_order_acquire);
    int last = atomic_load_explicit(&queue->last, memory_order_acquire);

    if ((current  + 1) % queue->size == last) {
        return NULL;
    }
    return queue->bufs[current];
}

void IRAM_ATTR lq_commit(LineQueue_t* queue) {

    int current = atomic_load_explicit(&queue->current, memory_order_acquire);

    if (current == queue->size - 1) {
        queue->current = 0;
    } else {
        atomic_fetch_add(&queue->current, 1);
    }

#ifdef RENDER_METHOD_LCD
    void epd_apply_line_mask_VE(uint8_t *line, const uint8_t *mask, int mask_len);
    epd_apply_line_mask_VE(queue->bufs[current], queue->mask_buffer, queue->mask_buffer_len);
#else
    for (int i=0; i < queue->mask_buffer_len / 4; i++) {
        ((uint32_t*)(queue->bufs[current]))[i] &= ((uint32_t*)(queue->mask_buffer))[i];
    }
#endif
}

int IRAM_ATTR lq_read(LineQueue_t* queue, uint8_t* dst) {
    int current = atomic_load_explicit(&queue->current, memory_order_acquire);
    int last = atomic_load_explicit(&queue->last, memory_order_acquire);

    if (current == last) {
        return -1;
    }

    memcpy(dst, queue->bufs[last], queue->element_size);

    if (last == queue->size - 1) {
        queue->last = 0;
    } else {
        atomic_fetch_add(&queue->last, 1);
    }
    return 0;
}


void IRAM_ATTR lq_reset(LineQueue_t* queue) {
    queue->current = 0;
    queue->last = 0;
}

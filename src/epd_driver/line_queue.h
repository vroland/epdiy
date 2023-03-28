#pragma once

#include <stdatomic.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    int size;
    atomic_int current;
    atomic_int last;
    uint8_t* buf;
    size_t element_size;
} LineQueue_t;

uint8_t* lq_current(LineQueue_t* queue);
void lq_commit(LineQueue_t* queue);
int lq_read(LineQueue_t* queue, uint8_t* dst);

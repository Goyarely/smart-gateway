#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdint.h>
#include <stddef.h>
#include "protocol.h"

#define RINGBUF_SIZE 4096

typedef struct {
    uint8_t buf[RINGBUF_SIZE];
    size_t head;                //写指针
    size_t tail;                //读指针
} ringbuf_t;

size_t ringbuf_write(ringbuf_t *rb, const uint8_t *data, size_t len);

size_t ringbuf_used(const ringbuf_t *rb);

void ring_init(ringbuf_t *rb);

int ringbuf_try_parse(ringbuf_t *rb, frame_t *out);

#endif 
#include "ringbuf.h"
#include <string.h>

void ring_init(ringbuf_t *rb) {
  rb->head = 0;
  rb->tail = 0;
}

size_t ringbuf_used(const ringbuf_t *rb) {
  return (rb->head >= rb->tail) ? (rb->head - rb->tail)
                                : (RINGBUF_SIZE - rb->tail + rb->head);
}

size_t ringbuf_write(ringbuf_t *rb, const uint8_t *data, size_t len) {
  size_t space = RINGBUF_SIZE - 1 - ringbuf_used(rb);

  for (size_t i = 0; i < len; i++) {
    rb->buf[rb->head] = data[i];
    rb->head = (rb->head + 1) % RINGBUF_SIZE;
  }

  return len;
}

/*偷看不消费*/
static void ringbuf_peek(const ringbuf_t *rb, size_t offset, uint8_t *out,
                         size_t len) {
  for (size_t i = 0; i < len; i++) {
    out[i] = rb->buf[(rb->tail + offset + i) % RINGBUF_SIZE];
  }
}

/*消费（丢弃头部len字节）*/
static void ringbuf_consume(ringbuf_t *rb, size_t len) {
  rb->tail = (rb->tail + len) % RINGBUF_SIZE;
}

int ringbuf_try_parse(ringbuf_t *rb, frame_t *out) {
  size_t used = ringbuf_used(rb);
  if (used < FRAME_HEAD_LEN)
    return 0;

  /*偷看尽量多的数据（最多一帧上限）,不消费*/
  size_t want = used;
  if (want > FRAME_HEAD_LEN + MAX_PAYLOAD) {
    want = FRAME_HEAD_LEN + MAX_PAYLOAD;
  }
  uint8_t tmp[FRAME_HEAD_LEN + MAX_PAYLOAD];
  ringbuf_peek(rb, 0, tmp, want);

  int ret = frame_decode(tmp, want, out);
  if (ret > 0) {
    // 拆出完整帧
    ringbuf_consume(rb, (size_t)ret);
    return 1;
  }

  if (ret == 0) {
    // 数据不足，等更多字节，坏帧跳1字节重新找帧头
    ringbuf_consume(rb, 1);
    return 0;
  }
  return -1;
}

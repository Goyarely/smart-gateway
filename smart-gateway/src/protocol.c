#include "protocol.h"
#include <string.h>

/* CRC-16/IBM 反射算法（多项式 0x8005 反射后 0xA001， 初值 0xFFFF）*/
uint16_t crc16(const uint8_t *data, uint16_t lenth) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < lenth; i++) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

/*大端读写*/
static void put16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}

static uint16_t get16(const uint8_t *p) {
  return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t get32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

int frame_encode(uint8_t *buf, size_t buf_size, uint8_t type, uint32_t dev_id,
                 uint16_t seq, const uint8_t *payload, uint16_t payload_len) {
  if (payload_len > MAX_PAYLOAD)
    return 0;
  size_t total = FRAME_HEAD_LEN + payload_len;
  if (buf_size < total)
    return 0;

  put16(buf, FRAME_MAGIC);     // 0.magic
  buf[2] = FRAME_VER;          // 2.ver
  put16(buf + 3, payload_len); // 3.len
  buf[5] = type;               // 5.type
  put32(buf + 6, dev_id);      // 6.dev_id
  put16(buf + 10, seq);        // 10.seq

  if (payload && payload_len > 0) {
    memcpy(buf + FRAME_HEAD_LEN, payload, payload_len);
  }

  /* CRC 覆盖：从帧头到payload(不含crc字段本身，即前12字节 + payload)*/
  uint16_t crc = crc16(buf, FRAME_HEAD_LEN - 2 + payload_len);
  put16(buf + 12, crc);

  return (int)total;
}

int frame_decode(const uint8_t *buf, size_t buf_size, frame_t *out) {
  if (buf_size < FRAME_HEAD_LEN)
    return 0; // 头都没齐

  if (get16(buf) != FRAME_MAGIC)
    return -1; // 帧头不对

  uint16_t payload_len = get16(buf + 3);
  if (payload_len > MAX_PAYLOAD)
    return 0; // 长度异常

  size_t total = FRAME_HEAD_LEN + payload_len;
  if (total > buf_size)
    return 0; // payload 还没到齐

  uint16_t crc_recv = get16(buf + 12);
  uint16_t crc_calc = crc16(buf, FRAME_HEAD_LEN - 2 + payload_len);
  if (crc_recv != crc_calc)
    return -1; // 校验失败：坏帧

  out->type = buf[5];
  out->dev_id = get32(buf + 6);
  out->seq = get16(buf + 10);
  out->playload_len = payload_len;
  if (payload_len > 0) {
    memcpy(out->payload, buf + FRAME_HEAD_LEN, payload_len);
  }

  return (int)total;
}

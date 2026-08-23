/**
 * @file test_protocol.c
 * 协议编解码自测：帧往返 + CRC 校验。
 */
#include <stdio.h>
#include <string.h>
#include "protocol.h"

static int pass = 0;
static int fail = 0;

#define CHECK(cond) do { \
    if(cond) { pass++; } \
    else { fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while(0)

static int test_roundtrip(uint8_t type, uint32_t dev, uint16_t seq,
                          const uint8_t * payload, size_t plen)
{
    uint8_t buf[FRAME_MAX_LEN];
    int n = frame_pack(buf, type, dev, seq, payload, plen);
    if(n < 0) return -1;

    uint8_t  o_type; uint32_t o_dev; uint16_t o_seq;
    const uint8_t * o_payload; size_t o_plen;
    if(frame_parse(buf, n, &o_type, &o_dev, &o_seq, &o_payload, &o_plen) != 0) return -1;

    if(o_type != type || o_dev != dev || o_seq != seq) return -1;
    if(o_plen != plen) return -1;
    if(plen && memcmp(o_payload, payload, plen) != 0) return -1;
    return 0;
}

int main(void)
{
    /* 无 payload */
    CHECK(test_roundtrip(CMD_REGISTER, PANEL_DEV_ID, 1, NULL, 0) == 0);
    /* 灯光 */
    uint8_t light = 80;
    CHECK(test_roundtrip(CMD_SET_LIGHT, PANEL_DEV_ID, 2, &light, 1) == 0);
    /* 风扇（2 字节） */
    uint8_t fan[2] = {0x07, 0x08}; /* 1800 大端 */
    CHECK(test_roundtrip(CMD_SET_FAN, PANEL_DEV_ID, 3, fan, 2) == 0);
    /* 场景 */
    uint8_t scene = 0x05;
    CHECK(test_roundtrip(CMD_SET_SCENE, PANEL_DEV_ID, 4, &scene, 1) == 0);
    /* 数据上报 */
    uint8_t report[2] = {0x01, 0x63}; /* sensor=1, value=99 */
    CHECK(test_roundtrip(CMD_DATA_REPORT, 0x00F00001, 5, report, 2) == 0);

    /* ===== 帧头字段校验（大端） ===== */
    uint8_t buf[FRAME_MAX_LEN];
    int n = frame_pack(buf, CMD_SET_LIGHT, PANEL_DEV_ID, 0x1234, &light, 1);
    CHECK(n == FRAME_HEAD_LEN + 1 + FRAME_CRC_LEN);   /* 12+1+2=15 */
    CHECK(buf[0] == 0xAA && buf[1] == 0x55);          /* magic */
    CHECK(buf[2] == 1);                               /* ver */
    /* len = 12 + 1 = 13, 大端 */
    CHECK(buf[3] == 0x00 && buf[4] == 0x0D);
    CHECK(buf[5] == CMD_SET_LIGHT);                   /* type */
    CHECK(buf[6] == 0x00 && buf[7] == 0xF1 && buf[8] == 0x00 && buf[9] == 0x01); /* dev_id */
    CHECK(buf[10] == 0x12 && buf[11] == 0x34);        /* seq */
    CHECK(buf[12] == 80);                             /* payload */
    /* crc 在 13-14，非零 */
    CHECK((buf[13] | buf[14]) != 0);

    /* ===== 篡改 payload 应被 CRC 拒绝 ===== */
    buf[12] = 90;  /* 改亮度 */
    {
        uint8_t  o_type; uint32_t o_dev; uint16_t o_seq;
        const uint8_t * o_payload; size_t o_plen;
        CHECK(frame_parse(buf, n, &o_type, &o_dev, &o_seq, &o_payload, &o_plen) == -1);
    }

    /* ===== 长度不符应拒绝 ===== */
    CHECK(frame_parse(buf, n - 1, NULL, NULL, NULL, NULL, NULL) == -1);
    /* 太短 */
    CHECK(frame_parse(buf, FRAME_HEAD_LEN, NULL, NULL, NULL, NULL, NULL) == -1);

    printf("结果: %d 通过, %d 失败\n", pass, fail);
    return fail == 0 ? 0 : 1;
}

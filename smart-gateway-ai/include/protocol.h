/**
 * @file protocol.h
 * @brief 网关二进制协议（面板侧，参照架构设计文档"七、通信协议"）
 *
 * 帧格式（大端，CRC 在帧尾）：
 *   magic(2) | ver(1) | len(2) | type(1) | dev_id(4) | seq(2) | payload(变长) | crc(2)
 *     0xAA55   ver=1    len=12+  type    dev_id     seq                      CRC16 覆盖整帧
 *
 * 说明：
 *   - 帧头固定 12 字节（magic2+ver1+len2+type1+dev_id4+seq2）
 *   - len = 12（头）+ payload 长度（不含 crc）
 *   - CRC16 覆盖从 magic 到 payload 末尾（即 len 字节），追加在帧尾
 *   - 面板 dev_id 建议 0x00F10001
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============ 帧布局常量 ============ */
#define FRAME_MAGIC          0xAA55
#define FRAME_VER            1
#define FRAME_HEAD_LEN       12          /* magic2+ver1+len2+type1+dev_id4+seq2 = 12 */
#define FRAME_CRC_LEN        2
#define FRAME_MAX_PAYLOAD    256
#define FRAME_MAX_LEN        (FRAME_HEAD_LEN + FRAME_CRC_LEN + FRAME_MAX_PAYLOAD)

/* ============ 命令类型 ============ */
#define CMD_REGISTER        0x01   /* 面板→网关   无 payload */
#define CMD_REGISTER_ACK    0x02   /* 网关→面板   result(1) */
#define CMD_HEARTBEAT       0x03   /* 面板→网关   无 payload */
#define CMD_SET_LIGHT       0x04   /* 面板→网关   亮度 0-100(1) */
#define CMD_SET_FAN         0x05   /* 面板→网关   转速 0-2400(2) */
#define CMD_SET_SCENE       0x06   /* 面板→网关   场景组合(1) */
#define CMD_DATA_REPORT     0x07   /* 设备→网关→面板  sensor+value(2) */
#define CMD_SET_ACK         0x08   /* 设备→面板   result(1) */
#define CMD_SET_CT          0x09   /* 面板→网关   色温 0-100(1) */
#define CMD_STAT            0x0A   /* 网关→面板   在线设备数(1) 状态通知 */

/* 面板 dev_id（独立于真实传感器设备） */
#define PANEL_DEV_ID        0x00F10001

/* ============ 帧结构 ============ */
#pragma pack(push, 1)
typedef struct {
    uint16_t magic;     /* 0xAA55 */
    uint8_t  ver;       /* 1 */
    uint16_t len;       /* 头部+payload 长度（不含 crc） */
    uint8_t  type;      /* 命令类型 */
    uint32_t dev_id;    /* 设备 ID（大端） */
    uint16_t seq;       /* 序号 */
    /* crc16 在 payload 之后，运行时单独处理 */
    uint8_t  payload[FRAME_MAX_PAYLOAD];
} frame_t;
#pragma pack(pop)

/* ============ 大端读写（供收帧拆包等使用） ============ */
uint16_t rd_be16(const uint8_t * p);
uint32_t rd_be32(const uint8_t * p);
void     wr_be16(uint8_t * p, uint16_t v);
void     wr_be32(uint8_t * p, uint32_t v);

/* ============ 编解码接口 ============ */

/** CRC16-CCITT(0x1021) 查表计算 */
uint16_t crc16(const uint8_t * data, size_t len);

/**
 * 编码一帧到 buf（含 CRC）。
 * @param buf       输出缓冲，至少 FRAME_HEAD_LEN + payload_len + FRAME_CRC_LEN
 * @param type      命令类型
 * @param dev_id    设备 ID
 * @param seq       序号
 * @param payload   负载
 * @param plen      负载长度
 * @return 帧总长度（含 crc），失败 -1
 */
int frame_pack(uint8_t * buf, uint8_t type, uint32_t dev_id, uint16_t seq,
               const uint8_t * payload, size_t plen);

/**
 * 解析并校验一帧。
 * @param frame  帧数据
 * @param flen   帧数据长度
 * @param out_type  输出：命令类型
 * @param out_dev   输出：dev_id
 * @param out_seq   输出：seq
 * @param out_payload 输出：指向 payload（不拷贝，指向 frame 内部）
 * @param out_plen    输出：payload 长度
 * @return 0 校验通过，-1 非法
 */
int frame_parse(const uint8_t * frame, size_t flen,
                uint8_t * out_type, uint32_t * out_dev, uint16_t * out_seq,
                const uint8_t ** out_payload, size_t * out_plen);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_H */

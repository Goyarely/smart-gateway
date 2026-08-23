#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

/* 帧常量*/
#define FRAME_MAGIC 0xAA55 /* 帧头魔数 */
#define FRAME_VER 0x01     /* 协议版本 */
#define MAX_PAYLOAD 256    /* 单帧载荷上限 */
#define FRAME_HEAD_LEN                                                         \
  14 /* 帧头固定长度：magic2+ver1+len2+type1+dev4+seq2+crc2 */

/*命令类型*/
enum {
  CMD_REGISTER = 0x01,     // 设备注册
  CMD_REGISTER_ACK = 0x02, // 注册应答
  CMD_HEARTBEAT = 0x03,    // 心跳保活
  CMD_SET_LIGHT = 0x04,    // 设置灯光亮度
  CMD_SET_FAN = 0x05,      // 设置风扇转速
  CMD_SET_SCENE = 0x06,    // 场景：亮度+色温
  CMD_DATA_REPORT = 0x07,  // 传感器上报
  CMD_SET_ACK = 0x08,      // 控制应答
};
/*帧结构*/
typedef struct {
  uint8_t type;          // 命令类型
  uint32_t dev_id;       // 设备ID
  uint16_t seq;          // 序列号
  uint16_t playload_len; // playload长度
  uint8_t payload[MAX_PAYLOAD];
} frame_t;

/*接口*/
uint16_t crc16(const uint8_t *data, uint16_t lenth);

/*组帧：序列化成write 格式写入buf，返回总帧长，失败返回-1*/
int frame_encode(uint8_t *buf, size_t buf_size, uint8_t type, uint32_t dev_id,
                 uint16_t seq, const uint8_t *payload, uint16_t payload_len);

/*拆帧：从buf解析一帧。返回总帧长（成功）/ 0（数据不足）/ -1（坏帧）*/
int frame_decode(const uint8_t *buf, size_t buf_size, frame_t *out);
#endif

/**
 * @file mqtt_client.h
 * @brief 自研 MQTT 3.1.1 客户端（无第三方库）
 *
 * 实现最小可用的 MQTT 3.1.1 子集：
 *  - CONNECT / CONNACK
 *  - PUBLISH（QoS0/1，QoS1 带 PACKET ID）
 *  - SUBSCRIBE / SUBACK
 *  - PINGREQ / PINGRESP（心跳保活）
 *  - DISCONNECT
 *
 * 非阻塞 socket + 状态机（连接/重连/心跳），可挂进 epoll 主循环，
 * 每次周期调用 mqtt_client_poll() 驱动。
 *
 * 配置：调用 mqtt_client_init() 后自动开始连接；断线指数退避重连。
 * broker 地址通过 GW_MQTT="host:port" 环境变量传入（未设置则不启用）。
 */
#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MQTT_TOPIC_MAX   64
#define MQTT_PAYLOAD_MAX 256
#define MQTT_RXBUF_MAX   1024

/* 收到云端 PUBLISH 时的回调：topic 以 '\0' 结尾，payload 带长度（不一定 '\0' 结尾） */
typedef void (*mqtt_msg_cb_t)(const char * topic,
                              const uint8_t * payload, int len,
                              void * user);

/**
 * @brief 初始化并启动连接
 * @param host broker 地址（IP 或域名）
 * @param port 端口
 * @param client_id  客户端 ID（可为 NULL，默认 "smart-gateway-ai"）
 * @param keepalive  保活秒数（0 用默认 30）
 * @param cb         云端消息回调（可为 NULL）
 * @param user       回调上下文
 */
void mqtt_client_init(const char * host, uint16_t port,
                      const char * client_id, uint16_t keepalive,
                      mqtt_msg_cb_t cb, void * user);

/** @brief 周期驱动：连接/收发/心跳/重连，放在主循环里调用 */
void mqtt_client_poll(void);

/** @brief 发布消息，qos 0 或 1 */
int  mqtt_client_publish(const char * topic, const void * payload, int len, uint8_t qos);

/** @brief 订阅主题（仅 QoS0） */
int  mqtt_client_subscribe(const char * topic);

bool mqtt_client_is_connected(void);

/** @brief 释放资源 */
void mqtt_client_deinit(void);

#endif /* MQTT_CLIENT_H */
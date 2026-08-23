/**
 * @file ui_bridge.h
 * @brief 面板 <-> 网关 协议客户端（ui_bridge）
 *
 * 职责：作为"面板设备"连到网关 server:8888，完成
 *   连接 -> 注册(REGISTER) -> 周期心跳(HEARTBEAT) -> 收帧/发命令。
 *
 * 与 UI 解耦：
 *   - 本模块不依赖 SquareLine 控件，通过 ui_bridge_on_status 回调通知 UI 状态
 *   - 控件事件回调（main.c 里）用 ui_bridge_send() 下发控制命令
 *
 * 单线程：在 LVGL 主循环里由 lv_timer 周期调用 ui_bridge_poll()，
 * socket 非阻塞，天然线程安全。
 */
#ifndef UI_BRIDGE_H
#define UI_BRIDGE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 连接状态回调：网关连接/注册状态变化时调用（用于更新 UI 显示）
 * @param text  状态文本（如 "已注册"、"网关未连接"、"连接网关..."、"网关断开"）
 */
typedef void (*ui_bridge_status_cb_t)(const char * text);

/** 初始化：设置网关地址，注册状态回调（需在 ui_init 之后调用） */
void ui_bridge_init(const char * host, int port, ui_bridge_status_cb_t status_cb);

/** 周期处理：连接/注册/心跳/收帧（在 lv_timer 回调里周期调用） */
void ui_bridge_poll(void);

/**
 * 发送一帧命令（控件事件回调里调用）
 * @param type    命令类型（CMD_SET_LIGHT 等，见 protocol.h）
 * @param payload 负载
 * @param plen    负载长度
 * @return 0 成功，-1 失败（未连接等）
 */
int ui_bridge_send(uint8_t type, const uint8_t * payload, size_t plen);

/** 当前是否已连上网关 */
bool ui_bridge_is_connected(void);

/** 当前是否已注册 */
bool ui_bridge_is_registered(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_BRIDGE_H */

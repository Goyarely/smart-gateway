/**
 * @file ui_state.h
 * @brief 设备状态共享模块（跨页面同步灯光/风扇数据与标签）
 *
 * 背景：Screen1(设备控制) / Screen2(灯光) / Screen3(风扇) 各自有
 * 独立控件控制同一个设备。本模块维护一份"全局设备状态"，
 * 任一页操作时统一更新状态 + 同步所有页的滑块/开关/标签，保证三页数据一致。
 *
 * 用法：
 *  - 各页控件回调调用 ui_state_set_light_bright() 等入口（不要直接操作其他页控件）
 *  - 模块内部负责刷新所有相关页的控件与标签
 */
#ifndef UI_STATE_H
#define UI_STATE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 设备状态结构 */
typedef struct {
    bool light_on;      /* 灯光开关 */
    int  light_bright;  /* 灯光亮度 0-100 */
    int  light_ct;      /* 色温 0-100（对应 2700~6500K） */
    bool fan_on;        /* 风扇开关 */
    int  fan_rpm;       /* 风扇转速 0-2400 */
} device_state_t;

/** 获取全局设备状态 */
device_state_t * ui_state_get(void);

/** 初始化（在 ui_init 之后调用，绑定各页控件引用） */
void ui_state_init(void);

/* ============ 灯光 ============ */

/** 设置灯光开关（同步各页 Switch + 状态标签 + 下发命令） */
void ui_state_set_light_on(bool on);

/** 设置灯光亮度 0-100（同步各页 Slider + 标签 + 下发命令） */
void ui_state_set_light_bright(int v);

/** 设置灯光色温 0-100（同步 Screen2 色温滑块 + 标签 + 下发命令） */
void ui_state_set_light_ct(int v);

/* ============ 风扇 ============ */

/** 设置风扇开关（on: 转速=上次值或 2400；off: 转速=0，同步各页） */
void ui_state_set_fan_on(bool on);

/** 设置风扇转速 0-2400（同步各页 Slider + RPM 标签 + 下发命令） */
void ui_state_set_fan_rpm(int rpm);

/* ============ 全量同步 ============ */

/** 按当前状态刷新所有页控件/标签（切屏或外部数据变化时调用） */
void ui_state_sync_all(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_STATE_H */

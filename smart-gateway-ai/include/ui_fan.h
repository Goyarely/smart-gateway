/**
 * @file ui_fan.h
 * @brief 风扇页(Screen3) 控制逻辑
 *
 * 绑定 Screen3 的控制控件，下发命令到网关：
 *  - 风速档位：Button30(底)/34(中)/35(高)/36(强劲) -> SET_FAN
 *  - 转速滑块：Slider6 -> SET_FAN(0-2400) + 更新 RPM 显示
 *  - 定时关闭：Button37(30min)/38(1h)/39(2h)/41(取消) -> 占位(协议暂未定)
 *  - 摇头 Switch6 / 夜间静音 Switch8 -> 占位
 */
#ifndef UI_FAN_H
#define UI_FAN_H

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化风扇页控制（在 ui_init() 之后调用） */
void ui_fan_page_init(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_FAN_H */

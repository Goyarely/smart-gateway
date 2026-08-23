/**
 * @file ui_setting.h
 * @brief 设置页(Screen5) UI 逻辑：显示网关信息
 *
 * 在 Screen5 空白区域动态添加 Label，显示网关 IP/端口/在线设备数/版本等。
 */
#ifndef UI_SETTING_H
#define UI_SETTING_H

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化设置页（在 ui_init() 之后调用） */
void ui_setting_page_init(void);

/** 更新在线设备数显示 */
void ui_setting_update_devices(int count);

#ifdef __cplusplus
}
#endif

#endif /* UI_SETTING_H */

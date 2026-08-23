/**
 * @file ui_light.h
 * @brief 灯光页(Screen2) 控制逻辑
 *
 * 绑定 Screen2 的控制控件，下发命令到网关：
 *  - 灯具单选（Button26/28/29，SquareLine 已实现 CHECKED 高亮）
 *  - 亮度 Slider2  -> SET_LIGHT(0-100)
 *  - 电源 Switch2  -> SET_LIGHT(开/关)
 *  - 色温 Slider7  -> 占位（协议暂无色温，复用 SET_SCENE）
 *  - 场景按钮      -> SET_SCENE
 */
#ifndef UI_LIGHT_H
#define UI_LIGHT_H

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化灯光页控制（在 ui_init() 之后调用） */
void ui_light_page_init(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_LIGHT_H */

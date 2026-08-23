/**
 * @file ui_state.c
 * @brief 设备状态共享模块实现
 *
 * 跨 Screen1/2/3 同步灯光和风扇的控件与标签：
 *   - 灯光：Screen1(Slider1/Switch1/Label4/Label5) + Screen2(Slider2/Switch2/Slider7/Label34/36/37/46/47)
 *   - 风扇：Screen1(Slider3/Switch3/Label7/Label8) + Screen3(Slider6/RPMlable/档位按钮)
 *
 * 约定：
 *   - 灯光亮度/色温统一用 0-100 表示
 *   - 风扇转速统一用 0-2400 表示；Screen1 的 Slider3 是 0-100%，换算 rpm = v*2400/100
 */
#include <stdio.h>
#include "lvgl/lvgl.h"
#include "ui.h"
#include "protocol.h"
#include "ui_bridge.h"
#include "ui_state.h"

/* 全局设备状态 */
static device_state_t g_state = {
    .light_on     = true,
    .light_bright = 80,
    .light_ct     = 20,      /* 约 4000K */
    .fan_on       = true,
    .fan_rpm      = 1200,
};

device_state_t * ui_state_get(void)
{
    return &g_state;
}

/* ============ 内部：风扇档位按钮数组（复用 ui_fan.c 的映射） ============ */
static lv_obj_t * fan_gear_btns[4];
static const int fan_gear_rpm[4] = { 600, 1200, 1800, 2400 };

/* 按 rpm 更新 Screen3 档位按钮选中（精确匹配则选中对应档位，否则全不选） */
static void sync_fan_gear(int rpm)
{
    if(!fan_gear_btns[0]) return;
    for(int i = 0; i < 4; i++) {
        lv_obj_remove_state(fan_gear_btns[i], LV_STATE_CHECKED);
    }
    for(int i = 0; i < 4; i++) {
        if(fan_gear_rpm[i] == rpm) {
            lv_obj_add_state(fan_gear_btns[i], LV_STATE_CHECKED);
            break;
        }
    }
}

/* ============ 灯光同步 ============ */

/* 刷新灯光所有控件/标签（依据 g_state） */
static void sync_light(void)
{
    int b = g_state.light_bright;

    /* Screen1 首页灯光 */
    if(ui_Slider1) lv_slider_set_value(ui_Slider1, b, LV_ANIM_OFF);
    if(ui_Switch1) {
        if(g_state.light_on) lv_obj_add_state(ui_Switch1, LV_STATE_CHECKED);
        else lv_obj_remove_state(ui_Switch1, LV_STATE_CHECKED);
    }
    if(ui_Label4) { char buf[8]; snprintf(buf, sizeof(buf), "%d%%", b); lv_label_set_text(ui_Label4, buf); }
    if(ui_Label5) lv_label_set_text(ui_Label5, g_state.light_on ? "开启" : "关闭");

    /* Screen2 灯光页 */
    if(ui_Slider2) lv_slider_set_value(ui_Slider2, b, LV_ANIM_OFF);
    if(ui_Slider7) lv_slider_set_value(ui_Slider7, g_state.light_ct, LV_ANIM_OFF);
    if(ui_Switch2) {
        if(g_state.light_on) lv_obj_add_state(ui_Switch2, LV_STATE_CHECKED);
        else lv_obj_remove_state(ui_Switch2, LV_STATE_CHECKED);
    }
    if(ui_Label34) lv_label_set_text(ui_Label34, g_state.light_on ? "电源:开" : "电源:关");
    if(ui_Label36) { char buf[24]; snprintf(buf, sizeof(buf), "亮度:%d%%", b); lv_label_set_text(ui_Label36, buf); }
    if(ui_Label46) { char buf[16]; snprintf(buf, sizeof(buf), "PWM:%d%%", b); lv_label_set_text(ui_Label46, buf); }
    if(ui_Label47) lv_label_set_text(ui_Label47, g_state.light_on ? "校验结果:OK" : "校验结果:--");
    if(ui_Label37) {
        /* 色温显示 2700~6500K */
        int k = 2700 + g_state.light_ct * (6500 - 2700) / 100;
        char buf[24]; snprintf(buf, sizeof(buf), "色温:%dK", k); lv_label_set_text(ui_Label37, buf);
    }
}

/* ============ 风扇同步 ============ */

/* 刷新风扇所有控件/标签（依据 g_state） */
static void sync_fan(void)
{
    int rpm = g_state.fan_rpm;

    /* Screen1 首页风扇（Slider3 是 0-100%） */
    if(ui_Slider3) {
        int v = rpm * 100 / 2400;
        lv_slider_set_value(ui_Slider3, v, LV_ANIM_OFF);
    }
    if(ui_Switch3) {
        if(g_state.fan_on) lv_obj_add_state(ui_Switch3, LV_STATE_CHECKED);
        else lv_obj_remove_state(ui_Switch3, LV_STATE_CHECKED);
    }
    if(ui_Label8) { char buf[16]; snprintf(buf, sizeof(buf), "%d%%", rpm * 100 / 2400); lv_label_set_text(ui_Label8, buf); }
    if(ui_Label7) lv_label_set_text(ui_Label7, g_state.fan_on ? "开启" : "关闭");

    /* Screen3 风扇页 */
    if(ui_Slider6) lv_slider_set_value(ui_Slider6, rpm, LV_ANIM_OFF);
    if(ui_RPMlable) { char buf[8]; snprintf(buf, sizeof(buf), "%d", rpm); lv_label_set_text(ui_RPMlable, buf); }
    sync_fan_gear(rpm);
}

/* ============ 对外接口：灯光 ============ */

void ui_state_set_light_on(bool on)
{
    g_state.light_on = on;
    if(!on) g_state.light_bright = 0;
    else if(g_state.light_bright == 0) g_state.light_bright = 80;
    sync_light();
    ui_bridge_send(CMD_SET_LIGHT, (const uint8_t[]){ on ? 100 : 0 }, 1);
}

void ui_state_set_light_bright(int v)
{
    if(v < 0) v = 0;
    if(v > 100) v = 100;
    g_state.light_bright = v;
    if(v > 0) g_state.light_on = true;
    else g_state.light_on = false;
    sync_light();
    uint8_t p = (uint8_t)v;
    ui_bridge_send(CMD_SET_LIGHT, &p, 1);
}

void ui_state_set_light_ct(int v)
{
    if(v < 0) v = 0;
    if(v > 100) v = 100;
    g_state.light_ct = v;
    sync_light();
    uint8_t p = (uint8_t)v;
    ui_bridge_send(CMD_SET_CT, &p, 1);
}

/* ============ 对外接口：风扇 ============ */

void ui_state_set_fan_on(bool on)
{
    g_state.fan_on = on;
    if(!on) g_state.fan_rpm = 0;
    else if(g_state.fan_rpm == 0) g_state.fan_rpm = 1200;
    sync_fan();
    int rpm = on ? g_state.fan_rpm : 0;
    uint8_t p[2] = { (uint8_t)(rpm >> 8), (uint8_t)(rpm & 0xFF) };
    ui_bridge_send(CMD_SET_FAN, p, 2);
}

void ui_state_set_fan_rpm(int rpm)
{
    if(rpm < 0) rpm = 0;
    if(rpm > 2400) rpm = 2400;
    g_state.fan_rpm = rpm;
    if(rpm > 0) g_state.fan_on = true;
    else g_state.fan_on = false;
    sync_fan();
    uint8_t p[2] = { (uint8_t)(rpm >> 8), (uint8_t)(rpm & 0xFF) };
    ui_bridge_send(CMD_SET_FAN, p, 2);
}

/* ============ 全量同步 ============ */

void ui_state_sync_all(void)
{
    sync_light();
    sync_fan();
}

/* ============ 初始化 ============ */

void ui_state_init(void)
{
    /* 缓存风扇档位按钮（Screen3） */
    fan_gear_btns[0] = ui_Button37;  /* 底 */
    fan_gear_btns[1] = ui_Button30;  /* 中 */
    fan_gear_btns[2] = ui_Button34;  /* 高 */
    fan_gear_btns[3] = ui_Button35;  /* 强劲 */

    /* 初始全量同步一次 */
    ui_state_sync_all();
}

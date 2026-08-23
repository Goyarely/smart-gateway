/**
 * @file ui_light.c
 * @brief 灯光页(Screen2) 控制逻辑实现
 *
 * 给 Screen2 的控制控件挂命令回调，通过 ui_bridge_send 下发到网关。
 * 控件（SquareLine 导出，已在 ui.h 声明）：
 *  - 灯具单选：Button26(客厅主灯)/Button28(卧室灯)/Button29(餐厅灯)，用 CHECKED 高亮
 *  - 电源 Switch2 / 亮度 Slider2 / 色温 Slider7
 *  - 场景 Button27(明亮)/Button31(阅读)/Button32(观影)/Button33(睡眠)
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "lvgl/lvgl.h"
#include "ui.h"
#include "protocol.h"
#include "ui_bridge.h"
#include "ui_light.h"
#include "ui_state.h"

/* 灯具名映射（对应 Button26/28/29） */
static const char * const g_lamp_names[3] = { "客厅主灯", "卧室灯", "餐厅灯" };
/* 灯具对应 dev_id（命令路由用，0 表示不区分，仅 UI 反馈） */
enum { LAMP_LIVING, LAMP_BEDROOM, LAMP_DINING };

/* 当前选中的灯具（默认客厅主灯） */
static int g_current_lamp = LAMP_LIVING;

/* 各灯具独立状态（亮度 0-100 / 开关 / 色温 0-100）。
 * 注意：LAMP_LIVING(客厅主灯) 的状态与 ui_state 的 g_state 共享，
 * 卧室/餐厅灯为独立本地状态，不与主页"客厅灯光"联动。 */
static struct lamp_st {
    bool on;
    int  bright;
    int  ct;
} g_lamp_state[3] = {
    { .on = true,  .bright = 80, .ct = 20 },   /* 客厅主灯：初值会被 ui_state 覆盖 */
    { .on = true,  .bright = 60, .ct = 30 },   /* 卧室灯 */
    { .on = true,  .bright = 50, .ct = 40 },   /* 餐厅灯 */
};

/* 更新当前灯具显示 + 控制区信息 */
static void update_lamp_info(int lamp_idx)
{
    char buf[32];
    if(ui_Label22) {
        snprintf(buf, sizeof(buf), "当前灯具:%s", g_lamp_names[lamp_idx]);
        lv_label_set_text(ui_Label22, buf);
    }
    if(ui_Label38) {
        snprintf(buf, sizeof(buf), "设备ID: 0x00F0000%d", lamp_idx + 1);
        lv_label_set_text(ui_Label38, buf);
    }
}

/* 把选中灯具的状态恢复到控件（Slider2/Switch2/Slider7 + 标签） */
static void apply_lamp_ui(int lamp)
{
    bool on;
    int b, ct;
    if(lamp == LAMP_LIVING) {
        /* 客厅主灯：状态来自 ui_state（与主页共享） */
        device_state_t * s = ui_state_get();
        on = s->light_on;
        b  = s->light_bright;
        ct = s->light_ct;
    } else {
        /* 卧室/餐厅灯：独立状态 */
        on = g_lamp_state[lamp].on;
        b  = g_lamp_state[lamp].bright;
        ct = g_lamp_state[lamp].ct;
    }
    if(ui_Slider2) lv_slider_set_value(ui_Slider2, b, LV_ANIM_OFF);
    if(ui_Slider7) lv_slider_set_value(ui_Slider7, ct, LV_ANIM_OFF);
    if(ui_Switch2) {
        if(on) lv_obj_add_state(ui_Switch2, LV_STATE_CHECKED);
        else lv_obj_remove_state(ui_Switch2, LV_STATE_CHECKED);
    }
    if(ui_Label34) lv_label_set_text(ui_Label34, on ? "电源:开" : "电源:关");
    if(ui_Label36) { char buf[24]; snprintf(buf, sizeof(buf), "亮度:%d%%", b); lv_label_set_text(ui_Label36, buf); }
    if(ui_Label46) { char buf[16]; snprintf(buf, sizeof(buf), "PWM:%d%%", b); lv_label_set_text(ui_Label46, buf); }
    if(ui_Label47) lv_label_set_text(ui_Label47, on ? "校验结果:OK" : "校验结果:--");
    if(ui_Label37) {
        int k = 2700 + ct * (6500 - 2700) / 100;
        char buf[24]; snprintf(buf, sizeof(buf), "色温:%dK", k); lv_label_set_text(ui_Label37, buf);
    }
}

/* 灯具选择：记录当前灯具 + 恢复该灯具状态到控件 */
static void lamp_select_cb(lv_event_t * e)
{
    int lamp = (int)(intptr_t)lv_event_get_user_data(e);
    g_current_lamp = lamp;
    update_lamp_info(lamp);
    apply_lamp_ui(lamp);
}

/* ===== 电源开关 =====
 * 仅选中"客厅主灯"时与主页(ui_state)联动；卧室/餐厅灯为独立本地状态 */
static void light_power_cb(lv_event_t * e)
{
    lv_obj_t * sw = lv_event_get_target_obj(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);

    if(g_current_lamp == LAMP_LIVING) {
        /* 客厅主灯：由 ui_state 统一同步主页 + Screen2 + 下发命令 */
        ui_state_set_light_on(on);
    } else {
        /* 卧室/餐厅灯：只更新本地状态 + 标签 + 下发命令，不与主页联动 */
        g_lamp_state[g_current_lamp].on = on;
        if(!on) g_lamp_state[g_current_lamp].bright = 0;
        if(ui_Label34) lv_label_set_text(ui_Label34, on ? "电源:开" : "电源:关");
        if(ui_Label47) lv_label_set_text(ui_Label47, on ? "校验结果:OK" : "校验结果:--");
        uint8_t p = on ? 100 : 0;
        ui_bridge_send(CMD_SET_LIGHT, &p, 1);
    }
}

/* ===== 亮度滑块 =====
 * 仅选中"客厅主灯"时与主页(ui_state)联动；卧室/餐厅灯为独立本地状态 */
static void light_bright_cb(lv_event_t * e)
{
    lv_obj_t * slider = lv_event_get_target_obj(e);
    int p = (int)lv_slider_get_value(slider);

    if(g_current_lamp == LAMP_LIVING) {
        /* 客厅主灯：由 ui_state 统一同步主页 + Screen2 + 下发命令 */
        ui_state_set_light_bright(p);
    } else {
        /* 卧室/餐厅灯：独立本地状态 */
        g_lamp_state[g_current_lamp].bright = p;
        if(p > 0) g_lamp_state[g_current_lamp].on = true;
        else g_lamp_state[g_current_lamp].on = false;
        if(ui_Label36) { char buf[24]; snprintf(buf, sizeof(buf), "亮度:%d%%", p); lv_label_set_text(ui_Label36, buf); }
        if(ui_Label46) { char buf[16]; snprintf(buf, sizeof(buf), "PWM:%d%%", p); lv_label_set_text(ui_Label46, buf); }
        if(ui_Label34) lv_label_set_text(ui_Label34, p > 0 ? "电源:开" : "电源:关");
        if(ui_Label47) lv_label_set_text(ui_Label47, p > 0 ? "校验结果:OK" : "校验结果:--");
        uint8_t v = (uint8_t)p;
        ui_bridge_send(CMD_SET_LIGHT, &v, 1);
    }
}

/* 色温滑块值(0-100) -> 开尔文(K)，映射 2700~6500K */
#define CT_MIN_K   2700
#define CT_MAX_K   6500
static int ct_slider_to_kelvin(uint8_t p)
{
    return CT_MIN_K + (int)p * (CT_MAX_K - CT_MIN_K) / 100;
}
/* 开尔文(K) -> 色温滑块值(0-100) */
static uint8_t ct_kelvin_to_slider(int kelvin)
{
    if(kelvin <= CT_MIN_K) return 0;
    if(kelvin >= CT_MAX_K) return 100;
    return (uint8_t)((kelvin - CT_MIN_K) * 100 / (CT_MAX_K - CT_MIN_K));
}

/* ===== 色温滑块 =====
 * 仅选中"客厅主灯"时与主页(ui_state)联动；卧室/餐厅灯为独立本地状态 */
static void light_ct_cb(lv_event_t * e)
{
    lv_obj_t * slider = lv_event_get_target_obj(e);
    int p = (int)lv_slider_get_value(slider);

    if(g_current_lamp == LAMP_LIVING) {
        /* 客厅主灯：由 ui_state 统一同步色温滑块 + 标签 + 下发命令 */
        ui_state_set_light_ct(p);
    } else {
        /* 卧室/餐厅灯：独立本地状态 */
        g_lamp_state[g_current_lamp].ct = p;
        if(ui_Label37) {
            int k = 2700 + p * (6500 - 2700) / 100;
            char buf[24]; snprintf(buf, sizeof(buf), "色温:%dK", k); lv_label_set_text(ui_Label37, buf);
        }
        uint8_t v = (uint8_t)p;
        ui_bridge_send(CMD_SET_CT, &v, 1);
    }
}

/* ===== 场景按钮 =====
 * 场景 = 一键下发亮度+色温组合（对齐演示网页 map），并同步更新滑块与显示：
 *   明亮 [100,5000K] / 阅读 [85,4000K] / 观影 [40,3000K] / 睡眠 [5,2700K]
 */
enum { SCENE_BRIGHT, SCENE_READ, SCENE_MOVIE, SCENE_SLEEP };
static const int g_scene_bright[4] = { 100, 85, 40, 5 };
static const int g_scene_kelvin[4] = { 5000, 4000, 3000, 2700 };

static void scene_btn_cb(lv_event_t * e)
{
    int scene = (int)(intptr_t)lv_event_get_user_data(e);
    if(scene < 0 || scene >= 4) scene = SCENE_BRIGHT;

    if(g_current_lamp == LAMP_LIVING) {
        /* 客厅主灯：场景由 ui_state 统一同步主页 + Screen2 */
        ui_state_set_light_on(true);
        ui_state_set_light_bright(g_scene_bright[scene]);
        ui_state_set_light_ct(ct_kelvin_to_slider(g_scene_kelvin[scene]));
    } else {
        /* 卧室/餐厅灯：只应用本地状态，不与主页联动 */
        g_lamp_state[g_current_lamp].on = true;
        g_lamp_state[g_current_lamp].bright = g_scene_bright[scene];
        g_lamp_state[g_current_lamp].ct = ct_kelvin_to_slider(g_scene_kelvin[scene]);
        apply_lamp_ui(g_current_lamp);
    }
    uint8_t p = (uint8_t)(scene + 1);
    ui_bridge_send(CMD_SET_SCENE, &p, 1);
}

void ui_light_page_init(void)
{
    /* 灯具选择：更新当前灯具显示 */
    lv_obj_add_event_cb(ui_Button26, lamp_select_cb, LV_EVENT_CLICKED, (void *)(intptr_t)LAMP_LIVING);
    lv_obj_add_event_cb(ui_Button28, lamp_select_cb, LV_EVENT_CLICKED, (void *)(intptr_t)LAMP_BEDROOM);
    lv_obj_add_event_cb(ui_Button29, lamp_select_cb, LV_EVENT_CLICKED, (void *)(intptr_t)LAMP_DINING);
    /* 默认显示客厅主灯 */
    update_lamp_info(LAMP_LIVING);

    /* 电源开关 */
    lv_obj_add_event_cb(ui_Switch2, light_power_cb, LV_EVENT_VALUE_CHANGED, NULL);
    /* 亮度 */
    lv_obj_add_event_cb(ui_Slider2, light_bright_cb, LV_EVENT_VALUE_CHANGED, NULL);
    /* 色温 */
    lv_obj_add_event_cb(ui_Slider7, light_ct_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 场景按钮：明亮/阅读/观影/睡眠 */
    lv_obj_add_event_cb(ui_Button27, scene_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)SCENE_BRIGHT);
    lv_obj_add_event_cb(ui_Button31, scene_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)SCENE_READ);
    lv_obj_add_event_cb(ui_Button32, scene_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)SCENE_MOVIE);
    lv_obj_add_event_cb(ui_Button33, scene_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)SCENE_SLEEP);
}

/**
 * @file ui_fan.c
 * @brief 风扇页(Screen3) 控制逻辑实现
 *
 * 控件（SquareLine 导出，经核对）：
 *  - 档位 Button37(底)/30(中)/34(高)/35(强劲)，用 CHECKED 单选
 *  - 转速 Slider6（Label RPMlable 显示 RPM）
 *  - 定时 Button41(30min)/36(1h)/38(2h)/39(取消)
 *  - Switch6(摇头)/Switch8(夜间静音)
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "lvgl/lvgl.h"
#include "ui.h"
#include "protocol.h"
#include "ui_bridge.h"
#include "ui_fan.h"
#include "ui_state.h"

/* 档位 -> 转速 */
enum { FAN_LOW, FAN_MID, FAN_HIGH, FAN_TURBO };
static const int g_gear_rpm[4] = { 600, 1200, 1800, 2400 };

/* 档位按钮数组（顺序：底/中/高/强劲） */
static lv_obj_t * g_gear_btns[4];

/* 定时按钮（30min/1h/2h/取消） */
static lv_obj_t * g_timer_btns[4];
/* 定时分钟数（0=取消） */
static const int g_timer_min[4] = { 30, 60, 120, 0 };

/* 当前定时状态 Label（动态创建） */
static lv_obj_t * g_timer_lbl = NULL;

/* 标志：程序性设置 slider（档位按钮触发），避免回调重复取消档位/重复发命令 */
static bool g_programmatic = false;

/* 档位按钮：设转速（由 ui_state 统一同步档位选中 + RPM + Screen1 + 下发命令） */
static void gear_btn_cb(lv_event_t * e)
{
    int gear = (int)(intptr_t)lv_event_get_user_data(e);
    ui_state_set_fan_rpm(g_gear_rpm[gear]);
}

/* 转速滑块：手动调节（由 ui_state 统一同步，非档位转速时档位自动取消） */
static void fan_slider_cb(lv_event_t * e)
{
    lv_obj_t * slider = lv_event_get_target_obj(e);
    int rpm = lv_slider_get_value(slider);
    /* 程序性设置（ui_state 同步）时不重复处理 */
    if(g_programmatic) return;
    ui_state_set_fan_rpm(rpm);
}

/* 更新定时状态显示 */
static void update_timer_label(int minutes)
{
    if(!g_timer_lbl) return;
    char buf[32];
    if(minutes <= 0) {
        snprintf(buf, sizeof(buf), "定时关闭：未定时");
    }
    else if(minutes < 60) {
        snprintf(buf, sizeof(buf), "定时关闭：%d 分钟", minutes);
    }
    else {
        snprintf(buf, sizeof(buf), "定时关闭：%d 小时", minutes / 60);
    }
    lv_label_set_text(g_timer_lbl, buf);
}

/* 定时按钮单选 */
static void timer_btn_cb(lv_event_t * e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    /* 单选 */
    for(int i = 0; i < 4; i++) {
        lv_obj_remove_state(g_timer_btns[i], LV_STATE_CHECKED);
    }
    lv_obj_add_state(g_timer_btns[idx], LV_STATE_CHECKED);
    update_timer_label(g_timer_min[idx]);
}

/* 摇头/夜间静音开关：更新对应 Label 状态显示 */
static void switch_state_cb(lv_event_t * e)
{
    lv_obj_t * sw = lv_event_get_target_obj(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    lv_obj_t * lbl = (idx == 0) ? ui_Label58 : ui_Label60;   /* 摇头模式/夜间静音 */
    if(lbl) {
        lv_label_set_text(lbl, on ? (idx == 0 ? "摇头:开" : "静音:开")
                                  : (idx == 0 ? "摇头:关" : "静音:关"));
    }
}

void ui_fan_page_init(void)
{
    /* 档位：Button37(底)/30(中)/34(高)/35(强劲) */
    g_gear_btns[0] = ui_Button37;
    g_gear_btns[1] = ui_Button30;
    g_gear_btns[2] = ui_Button34;
    g_gear_btns[3] = ui_Button35;

    lv_obj_add_event_cb(ui_Button37, gear_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)FAN_LOW);
    lv_obj_add_event_cb(ui_Button30, gear_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)FAN_MID);
    lv_obj_add_event_cb(ui_Button34, gear_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)FAN_HIGH);
    lv_obj_add_event_cb(ui_Button35, gear_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)FAN_TURBO);

    /* 转速滑块（Slider6 范围设为 0-2400） */
    if(ui_Slider6) {
        lv_slider_set_range(ui_Slider6, 0, 2400);
        lv_obj_add_event_cb(ui_Slider6, fan_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }

    /* 定时按钮：Button41(30min)/36(1h)/38(2h)/39(取消) */
    g_timer_btns[0] = ui_Button41;
    g_timer_btns[1] = ui_Button36;
    g_timer_btns[2] = ui_Button38;
    g_timer_btns[3] = ui_Button39;

    lv_obj_add_event_cb(ui_Button41, timer_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)0);
    lv_obj_add_event_cb(ui_Button36, timer_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    lv_obj_add_event_cb(ui_Button38, timer_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)2);
    lv_obj_add_event_cb(ui_Button39, timer_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)3);

    /* 动态创建定时状态显示 Label（放在 Screen3 定时区域下方） */
    if(ui_Screen3) {
        g_timer_lbl = lv_label_create(ui_Screen3);
        lv_obj_set_style_text_font(g_timer_lbl, &ui_font_Font1, 0);
        lv_obj_set_style_text_color(g_timer_lbl, lv_color_black(), 0);
        lv_obj_set_pos(g_timer_lbl, 120, 440);
        update_timer_label(0);
    }

    /* Switch6(摇头) / Switch8(夜间静音)：更新状态显示（协议暂未定命令，仅 UI） */
    lv_obj_add_event_cb(ui_Switch6, switch_state_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)0);
    lv_obj_add_event_cb(ui_Switch8, switch_state_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)1);
}

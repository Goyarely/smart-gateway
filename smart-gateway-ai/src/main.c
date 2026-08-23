/**
 * @file main.c
 * LVGL 控制面板业务入口
 *
 * 流程：
 *   driver_backends_register() -> lv_init() -> 建默认 group
 *   -> driver_backends_init_backend("SDL"/"FBDEV") -> ui_init()
 *   -> ui_bridge_init(host, port, 状态回调) 连网关
 *   -> lv_timer 周期调 ui_bridge_poll() 收帧+心跳
 *   -> driver_backends_run_loop()
 *
 * 职责划分：
 *   - 本文件：LVGL 初始化、后端选择、SquareLine 控件命令绑定
 *   - src/ui_bridge.c：网关协议客户端（连接/注册/心跳/收帧/发命令）
 *   - src/protocol.c：帧编解码 + CRC16
 */
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "lvgl/lvgl.h"
#include "driver_backends.h"
#include "simulator_util.h"
#include "simulator_settings.h"

#include "ui.h"          /* SquareLine 导出界面 */
#include "protocol.h"
#include "ui_bridge.h"
#include "ui_data.h"
#include "ui_light.h"
#include "ui_fan.h"
#include "ui_setting.h"
#include "ui_chart.h"
#include "ui_state.h"

#define ENABLE_SQUARELINE_UI 1   /* 1=用 SquareLine 导出界面，0=用内置演示界面 */

/* 网关地址：默认连本机（PC 作为服务器），板子运行时可指定 PC 的 IP */
#define GW_HOST_DEFAULT "127.0.0.1"
#define GW_PORT_DEFAULT 8888

static char g_gw_host[64] = GW_HOST_DEFAULT;

/* 默认显示后端：SDL（PC 调试窗口）；板子落地用 -b FBDEV */
static const char * selected_backend = "SDL";

/* 全局设置（定义在 driver_backends.c） */
extern simulator_settings_t settings;

/* ==================== 演示界面（未导出 SquareLine 前先跑通） ============ */
static void demo_ui_create(void)
{
    lv_obj_t * scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);

    lv_obj_t * t = lv_label_create(scr);
    lv_label_set_text(t, "smart-gateway-ai 面板");
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t * s = lv_label_create(scr);
    lv_label_set_text(s, "LVGL 9.x + FBDEV/EVDEV 运行正常");
    lv_obj_set_style_text_color(s, lv_color_hex(0x6FE3C0), 0);
    lv_obj_set_style_text_font(s, &lv_font_montserrat_16, 0);
    lv_obj_align(s, LV_ALIGN_CENTER, 0, 20);
}

/* ==================== 网关状态显示（ui_bridge 状态回调） ================ */
static const char * g_last_status = NULL;
static void on_bridge_status(const char * text)
{
    /* 状态文本未变化则不重绘，避免"网关断开/未连接"反复横跳导致闪烁 */
    if(g_last_status && text && strcmp(g_last_status, text) == 0) return;
    g_last_status = text;
    if(ui_Label21) lv_label_set_text(ui_Label21, text);
}

/* ==================== 控件事件回调：下发命令 ========================== */

/* 设备类型 user_data */
enum { DEV_LIGHT, DEV_FAN, DEV_CURTAIN, DEV_AC };

/* Slider 变化：灯光亮度 / 风扇转速（% 映射 0-2400） / 窗帘 / 空调 */
static void slider_cmd_cb(lv_event_t * e)
{
    lv_obj_t * slider = lv_event_get_target_obj(e);
    int dev = (int)(intptr_t)lv_event_get_user_data(e);
    int val = lv_slider_get_value(slider);

    switch(dev) {
    case DEV_LIGHT:
        /* 灯光亮度：由 ui_state 统一更新状态 + 同步两页 + 下发 */
        ui_state_set_light_bright(val);
        break;
    case DEV_FAN:
        /* 风扇转速：Screen1 滑块 0-100% -> 0-2400 */
        ui_state_set_fan_rpm(val * 2400 / 100);
        break;
    case DEV_CURTAIN: case DEV_AC: {
        /* 窗帘/空调：场景组合（简化），只更新本页标签 */
        lv_obj_t * pct_lbl = (dev == DEV_CURTAIN) ? ui_Label14 : ui_Label18;
        lv_obj_t * st_lbl  = (dev == DEV_CURTAIN) ? ui_Label13 : ui_Label17;
        if(pct_lbl) { char b[16]; snprintf(b, sizeof(b), "%d%%", val); lv_label_set_text(pct_lbl, b); }
        if(st_lbl)  lv_label_set_text(st_lbl, val > 0 ? "开启" : "关闭");
        uint8_t p = (uint8_t)val;
        ui_bridge_send(CMD_SET_SCENE, &p, 1);
        break;
    }
    default: break;
    }
}

/* Switch 开关：开/关。灯光/风扇交由 ui_state 统一同步多页，窗帘/空调保持本地更新 */
static void switch_cmd_cb(lv_event_t * e)
{
    lv_obj_t * sw = lv_event_get_target_obj(e);
    int dev = (int)(intptr_t)lv_event_get_user_data(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);

    switch(dev) {
    case DEV_LIGHT:
        ui_state_set_light_on(on);
        break;
    case DEV_FAN:
        ui_state_set_fan_on(on);
        break;
    case DEV_CURTAIN: case DEV_AC: {
        lv_obj_t * st_lbl = (dev == DEV_CURTAIN) ? ui_Label13 : ui_Label17;
        if(st_lbl) lv_label_set_text(st_lbl, on ? "开启" : "关闭");
        uint8_t p = on ? 1 : 0;
        ui_bridge_send(CMD_SET_SCENE, &p, 1);
        break;
    }
    default: break;
    }
}

/* 给 SquareLine 导出的控件挂命令回调（不改导出文件） */
static void bind_device_controls(void)
{
    lv_obj_add_event_cb(ui_Slider1, slider_cmd_cb, LV_EVENT_VALUE_CHANGED,
                        (void *)(intptr_t)DEV_LIGHT);
    lv_obj_add_event_cb(ui_Switch1, switch_cmd_cb, LV_EVENT_VALUE_CHANGED,
                        (void *)(intptr_t)DEV_LIGHT);

    lv_obj_add_event_cb(ui_Slider3, slider_cmd_cb, LV_EVENT_VALUE_CHANGED,
                        (void *)(intptr_t)DEV_FAN);
    lv_obj_add_event_cb(ui_Switch3, switch_cmd_cb, LV_EVENT_VALUE_CHANGED,
                        (void *)(intptr_t)DEV_FAN);

    lv_obj_add_event_cb(ui_Slider4, slider_cmd_cb, LV_EVENT_VALUE_CHANGED,
                        (void *)(intptr_t)DEV_CURTAIN);
    lv_obj_add_event_cb(ui_Switch4, switch_cmd_cb, LV_EVENT_VALUE_CHANGED,
                        (void *)(intptr_t)DEV_CURTAIN);

    lv_obj_add_event_cb(ui_Slider5, slider_cmd_cb, LV_EVENT_VALUE_CHANGED,
                        (void *)(intptr_t)DEV_AC);
    lv_obj_add_event_cb(ui_Switch5, switch_cmd_cb, LV_EVENT_VALUE_CHANGED,
                        (void *)(intptr_t)DEV_AC);
}

/* ==================== 网关帧收发（lv_timer 周期回调） =================== */
static void ui_poll_cb(lv_timer_t * t)
{
    (void)t;
    ui_bridge_poll();   /* 连接/注册/心跳/收帧 */
}

/* ==================== 后端选择 ==================== */
/* 用法：./lvglsim                 -> SDL 窗口（PC 调试）
 *       ./lvglsim -b FBDEV       -> 帧缓冲（板子落地）
 *       ./lvglsim -h 192.168.x.x -> 指定网关(PC)IP
 *       ./lvglsim -W 1024 -H 600 -> 自定义窗口尺寸
 */
static void configure_simulator(int argc, char ** argv)
{
    int opt;
    driver_backends_register();

    settings.window_width  = 1024;
    settings.window_height = 600;

    const char * env_gw = getenv("GW_HOST");   /* 环境变量可指定网关 IP */
    if(env_gw && *env_gw) {
        snprintf(g_gw_host, sizeof(g_gw_host), "%s", env_gw);
    }

    while((opt = getopt(argc, argv, "b:h:W:H:")) != -1) {
        switch(opt) {
            case 'b':
                if(driver_backends_is_supported(optarg) == 0) {
                    die("error no such backend: %s\n", optarg);
                }
                selected_backend = optarg;
                break;
            case 'h':   /* 网关(本机服务器) IP */
                snprintf(g_gw_host, sizeof(g_gw_host), "%s", optarg);
                break;
            case 'W': settings.window_width  = atoi(optarg); break;
            case 'H': settings.window_height = atoi(optarg); break;
            case '?': die("unknown option -%c\n", optopt);
        }
    }
}

/* ==================== 入口 ==================== */
int main(int argc, char ** argv)
{
    configure_simulator(argc, argv);
    lv_init();

    /* SDL/FBDEV 键盘需要默认 group */
    if(lv_group_get_default() == NULL) {
        lv_group_t * grp = lv_group_create();
        lv_group_set_default(grp);
    }

    if(driver_backends_init_backend((char *)selected_backend) == -1) {
        die("Failed to initialize backend: %s\n", selected_backend);
    }
#if LV_USE_EVDEV
    if(driver_backends_init_backend("EVDEV") == -1) {
        LV_LOG_WARN("Failed to initialize evdev");
    }
#endif

    if(ENABLE_SQUARELINE_UI) {
        ui_init();             /* SquareLine 导出的界面（ui_Screen1） */
        ui_data_page_init();   /* 数据页(Screen4) 用 lv_list 填充历史数据/告警 */
        ui_light_page_init();  /* 灯光页(Screen2) 控件命令绑定 */
        ui_fan_page_init();    /* 风扇页(Screen3) 控件命令绑定 */
        ui_setting_page_init();/* 设置页(Screen5) 网关信息显示 */
        ui_chart_page_init();  /* 首页温度趋势图 + 数据页迷你柱状图 */
    }
    else {
        demo_ui_create();      /* 内置演示界面 */
    }

    /* 给控件挂命令回调 + 连网关 */
    bind_device_controls();
    ui_state_init();   /* 建立共享设备状态，并全量同步三页控件/标签 */
    ui_bridge_init(g_gw_host, GW_PORT_DEFAULT, on_bridge_status);

    /* 周期收帧 + 心跳（50ms） */
    lv_timer_create(ui_poll_cb, 50, NULL);

    driver_backends_run_loop();
    return 0;
}

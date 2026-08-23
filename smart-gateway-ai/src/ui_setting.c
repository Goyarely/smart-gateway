/**
 * @file ui_setting.c
 * @brief 设置页(Screen5) UI 逻辑实现
 *
 * 在 Screen5 空白区域动态添加 Label，分两组并排显示：
 *  左列：网络设置 + 设备管理
 *  右列：MQTT 云端 + 系统信息（含运行时长）
 * 信息项对齐演示网页 index.html 的"设置"页。
 */
#include <stdio.h>
#include <string.h>
#include "lvgl/lvgl.h"
#include "ui.h"
#include "ui_setting.h"

/* 中文字体 */
#define CN_FONT  (&ui_font_Font1)

/* 标签颜色 */
#define COLOR_TITLE  lv_color_hex(0x1E88E5)   /* 蓝色：panel 标题 */
#define COLOR_LABEL  lv_color_hex(0x555555)   /* 灰色：键名 */
#define COLOR_VALUE  lv_color_black()         /* 黑色：值/动态项 */

/* 屏幕布局（1024x600，左侧导航 80px） */
#define LBL_X_L     110    /* 左列内容起点 */
#define LBL_X_L_K   110    /* 左列"键"（如 "IP  : "）起点 */
#define LBL_X_L_V   200    /* 左列"值"（如 "10.122.117.172"）起点 */
#define LBL_X_R     540    /* 右列内容起点 */
#define LBL_X_R_K   540    /* 右列"键"起点 */
#define LBL_X_R_V   630    /* 右列"值"起点 */
#define ROW_H       32     /* 行高 */
#define TITLE_H     36     /* panel 标题高度 */

static lv_obj_t * g_lbl_devices = NULL;   /* 在线设备数（动态更新） */
static lv_obj_t * g_lbl_uptime  = NULL;   /* 运行时长（每秒更新） */
static uint32_t   g_boot_ms     = 0;      /* 启动时刻(lv_tick) */

void ui_setting_update_devices(int count)
{
    if(!g_lbl_devices) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "%d 台", count);
    lv_label_set_text(g_lbl_devices, buf);
}

/* 运行时长计时（lv_timer 周期回调） */
static void uptime_cb(lv_timer_t * t)
{
    (void)t;
    if(!g_lbl_uptime) return;
    uint32_t sec = (lv_tick_get() - g_boot_ms) / 1000;
    uint32_t d = sec / 86400, h = (sec % 86400) / 3600, m = (sec % 3600) / 60;
    char buf[40];
    snprintf(buf, sizeof(buf), "%u 天 %u 时 %u 分", d, h, m);
    lv_label_set_text(g_lbl_uptime, buf);
}

/* 创建 panel 标题（蓝色，size 略大） */
static lv_obj_t * add_title(int x, int y, const char * text)
{
    lv_obj_t * lbl = lv_label_create(ui_Screen5);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, CN_FONT, 0);
    lv_obj_set_style_text_color(lbl, COLOR_TITLE, 0);
    lv_obj_set_pos(lbl, x, y);
    return lbl;
}

/* 创建一行：键（灰） + 值（黑） */
static void add_row(int x_k, int x_v, int y, const char * key, const char * value)
{
    if(key) {
        lv_obj_t * k = lv_label_create(ui_Screen5);
        lv_label_set_text(k, key);
        lv_obj_set_style_text_font(k, CN_FONT, 0);
        lv_obj_set_style_text_color(k, COLOR_LABEL, 0);
        lv_obj_set_pos(k, x_k, y);
    }
    if(value) {
        lv_obj_t * v = lv_label_create(ui_Screen5);
        lv_label_set_text(v, value);
        lv_obj_set_style_text_font(v, CN_FONT, 0);
        lv_obj_set_style_text_color(v, COLOR_VALUE, 0);
        lv_obj_set_pos(v, x_v, y);
    }
}

/* 创建一行：键 + 可动态更新的 value Label（返回 value Label） */
static lv_obj_t * add_row_dyn(int x_k, int x_v, int y, const char * key, const char * init_value)
{
    add_row(x_k, 0, y, key, NULL);
    lv_obj_t * v = lv_label_create(ui_Screen5);
    lv_label_set_text(v, init_value);
    lv_obj_set_style_text_font(v, CN_FONT, 0);
    lv_obj_set_style_text_color(v, COLOR_VALUE, 0);
    lv_obj_set_pos(v, x_v, y);
    return v;
}

void ui_setting_page_init(void)
{
    /* 防止重复初始化 */
    if(g_lbl_devices && lv_obj_is_valid(g_lbl_devices)) return;
    if(ui_Screen5 == NULL) return;

    int y = 70;   /* 左列起始 y（panel 标题） */

    /* ===== 左列：网络设置 ===== */
    add_title(LBL_X_L, y, "网络设置");
    y += TITLE_H;
    add_row(LBL_X_L_K, LBL_X_L_V, y, "IP   :", "10.122.117.172"); y += ROW_H;
    add_row(LBL_X_L_K, LBL_X_L_V, y, "端口 :", "8888");           y += ROW_H;
    add_row(LBL_X_L_K, LBL_X_L_V, y, "IO   :", "epoll (ET)");     y += ROW_H;
    add_row(LBL_X_L_K, LBL_X_L_V, y, "最大连接:", "128");          y += ROW_H + 16;

    /* ===== 左列：设备管理 ===== */
    add_title(LBL_X_L, y, "设备管理");
    y += TITLE_H;
    add_row(LBL_X_L_K, LBL_X_L_V, y, "设备表 :", "哈希表");        y += ROW_H;
    add_row(LBL_X_L_K, LBL_X_L_V, y, "心跳间隔:", "2s");           y += ROW_H;
    add_row(LBL_X_L_K, LBL_X_L_V, y, "超时剔除:", "5s");           y += ROW_H;
    add_row(LBL_X_L_K, 0,        y, "在线设备:", NULL);
    g_lbl_devices = lv_label_create(ui_Screen5);
    lv_label_set_text(g_lbl_devices, "--");
    lv_obj_set_style_text_font(g_lbl_devices, CN_FONT, 0);
    lv_obj_set_style_text_color(g_lbl_devices, COLOR_VALUE, 0);
    lv_obj_set_pos(g_lbl_devices, LBL_X_L_V, y);

    /* ===== 右列：MQTT 云端 ===== */
    y = 70;
    add_title(LBL_X_R, y, "MQTT 云端");
    y += TITLE_H;
    add_row(LBL_X_R_K, LBL_X_R_V, y, "Broker  :", "--:1883");    y += ROW_H;
    add_row(LBL_X_R_K, LBL_X_R_V, y, "协议    :", "MQTT 3.1.1");  y += ROW_H;
    add_row(LBL_X_R_K, LBL_X_R_V, y, "QoS     :", "上报 QoS1");  y += ROW_H;
    add_row(LBL_X_R_K, LBL_X_R_V, y, "连接状态:", "未接入");      y += ROW_H + 16;

    /* ===== 右列：系统信息 ===== */
    add_title(LBL_X_R, y, "系统信息");
    y += TITLE_H;
    add_row(LBL_X_R_K, LBL_X_R_V, y, "固件版本:", "v1.2.0");      y += ROW_H;
    add_row(LBL_X_R_K, LBL_X_R_V, y, "日志    :", "INFO 环形缓冲");y += ROW_H;
    add_row(LBL_X_R_K, LBL_X_R_V, y, "存储    :", "A/B 双分区");   y += ROW_H;
    add_row(LBL_X_R_K, 0,        y, "运行时长:", NULL);
    g_boot_ms = lv_tick_get();
    g_lbl_uptime = lv_label_create(ui_Screen5);
    lv_label_set_text(g_lbl_uptime, "0 天 0 时 0 分");
    lv_obj_set_style_text_font(g_lbl_uptime, CN_FONT, 0);
    lv_obj_set_style_text_color(g_lbl_uptime, COLOR_VALUE, 0);
    lv_obj_set_pos(g_lbl_uptime, LBL_X_R_V, y);
    lv_timer_create(uptime_cb, 1000, NULL);

    /* 初始刷新一次 */
    uptime_cb(NULL);
}

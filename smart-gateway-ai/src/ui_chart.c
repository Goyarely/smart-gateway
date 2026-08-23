/**
 * @file ui_chart.c
 * @brief 图表模块实现
 *
 * Screen1：Container3 里放温度趋势折线图（lv_chart）
 * Screen4：Container21/22/23 里放温度/湿度/功耗迷你柱状图
 */
#include <stdio.h>
#include "lvgl/lvgl.h"
#include "ui.h"
#include "ui_chart.h"

/* Screen1 温度趋势图 */
static lv_obj_t * g_temp_chart = NULL;
static lv_chart_series_t * g_temp_ser = NULL;
#define TEMP_POINTS 20

/* Screen4 迷你柱状图 */
static lv_obj_t * g_mini_chart[3] = { NULL, NULL, NULL };
static lv_chart_series_t * g_mini_ser[3] = { NULL, NULL, NULL };

/* ===== Screen1 温度趋势折线图 ===== */
static void create_temp_trend(void)
{
    /* 放在 Container3 内，Label9（温度趋势标题）下方 */
    g_temp_chart = lv_chart_create(ui_Container3);
    lv_obj_set_size(g_temp_chart, 420, 250);
    lv_obj_set_pos(g_temp_chart, 15, 55);
    lv_obj_set_style_bg_color(g_temp_chart, lv_color_white(), 0);
    lv_chart_set_type(g_temp_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(g_temp_chart, TEMP_POINTS);
    lv_chart_set_range(g_temp_chart, LV_CHART_AXIS_PRIMARY_Y, 15, 35);  /* 15~35°C */
    lv_chart_set_div_line_count(g_temp_chart, 4, 10);
    g_temp_ser = lv_chart_add_series(g_temp_chart, lv_color_hex(0x2196F3),
                                     LV_CHART_AXIS_PRIMARY_Y);
}

void ui_chart_add_temp(float temp)
{
    if(!g_temp_chart || !g_temp_ser) return;
    lv_chart_set_next_value(g_temp_chart, g_temp_ser, (lv_coord_t)temp);
}

/* ===== Screen4 迷你柱状图 ===== */
static void create_mini_chart(int idx, lv_obj_t * container, lv_color_t color)
{
    lv_obj_t * c = lv_chart_create(container);
    /* 放在容器底部区域（上方留出数值 Label） */
    lv_obj_set_size(c, 250, 70);
    lv_obj_set_pos(c, 10, 70);
    lv_chart_set_type(c, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(c, 7);
    lv_chart_set_range(c, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(c, 2, 0);
    lv_obj_set_style_bg_color(c, lv_color_white(), 0);
    g_mini_chart[idx] = c;
    g_mini_ser[idx] = lv_chart_add_series(c, color, LV_CHART_AXIS_PRIMARY_Y);
    /* 初始示例数据 */
    for(int i = 0; i < 7; i++) {
        lv_chart_set_next_value(c, g_mini_ser[idx], 40 + i * 5);
    }
}

void ui_chart_set_mini(int idx, int val)
{
    if(idx < 0 || idx >= 3) return;
    if(!g_mini_chart[idx] || !g_mini_ser[idx]) return;
    lv_chart_set_next_value(g_mini_chart[idx], g_mini_ser[idx], val);
}

/* ===== 初始化 ===== */
void ui_chart_page_init(void)
{
    /* 防止重复初始化 */
    if(g_temp_chart && lv_obj_is_valid(g_temp_chart)) return;

    if(ui_Screen1 && ui_Container3) {
        create_temp_trend();
    }
    if(ui_Screen4) {
        if(ui_Container21) create_mini_chart(0, ui_Container21, lv_color_hex(0xF44336));  /* 温度红 */
        if(ui_Container22) create_mini_chart(1, ui_Container22, lv_color_hex(0x2196F3));  /* 湿度蓝 */
        if(ui_Container23) create_mini_chart(2, ui_Container23, lv_color_hex(0x4CAF50));  /* 功耗绿 */
    }
}

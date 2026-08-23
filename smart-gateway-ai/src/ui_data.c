/**
 * @file ui_data.c
 * @brief 数据页(Screen4) UI 逻辑
 *
 * 在 ui_init() 后调用 ui_data_page_init()：
 *  - ui_TabView1：加"历史数据"tab(lv_table) + "温度趋势"tab(lv_chart)
 *  - ui_TabView3：加"告警记录"tab(lv_list)
 */
#include <stdio.h>
#include <string.h>
#include "lvgl/lvgl.h"
#include "ui.h"
#include "ui_data.h"

static lv_obj_t * g_alarm_list   = NULL;
static lv_obj_t * g_history_tbl  = NULL;

/* SquareLine 中文字体（ui_font_Font1） */
#define CN_FONT  (&ui_font_Font1)

/* 表格行数（含表头 1 行，数据最多 29 条） */
#define TABLE_MAX_ROWS 30

static int g_data_rows = 0;   /* 当前数据行数（不含表头） */

/* ===== lv_table：历史数据 ===== */
void ui_data_add_history(const char * time, const char * dev,
                         const char * type, float value, const char * unit)
{
    if(!g_history_tbl) return;

    char val[16];
    snprintf(val, sizeof(val), "%.1f %s", value, unit ? unit : "");

    if(g_data_rows >= TABLE_MAX_ROWS - 1) {
        /* 满了：整体上移一行，丢弃最旧的（第 1 行），末行写新数据 */
        for(int r = 1; r < TABLE_MAX_ROWS - 1; r++) {
            for(int c = 0; c < 4; c++) {
                const char * cell = lv_table_get_cell_value(g_history_tbl, r + 1, c);
                lv_table_set_cell_value(g_history_tbl, r, c, cell);
            }
        }
        int last = TABLE_MAX_ROWS - 1;
        lv_table_set_cell_value(g_history_tbl, last, 0, time);
        lv_table_set_cell_value(g_history_tbl, last, 1, dev);
        lv_table_set_cell_value(g_history_tbl, last, 2, type);
        lv_table_set_cell_value(g_history_tbl, last, 3, val);
    }
    else {
        /* 未满：写到下一个空行 */
        int row = g_data_rows + 1;   /* 表头占第 0 行 */
        lv_table_set_cell_value(g_history_tbl, row, 0, time);
        lv_table_set_cell_value(g_history_tbl, row, 1, dev);
        lv_table_set_cell_value(g_history_tbl, row, 2, type);
        lv_table_set_cell_value(g_history_tbl, row, 3, val);
        g_data_rows++;
    }
}

/* ===== lv_list：告警记录 ===== */
void ui_data_add_alarm(const char * time, const char * msg)
{
    if(!g_alarm_list) return;
    char text[96];
    snprintf(text, sizeof(text), "%s    %s", time, msg);
    lv_obj_t * btn = lv_list_add_button(g_alarm_list, NULL, text);
    /* 按钮文本必须单独设中文字体（list 的 ITEMS 样式不作用于动态按钮） */
    lv_obj_set_style_text_font(btn, CN_FONT, 0);
    lv_obj_set_style_text_color(btn, lv_color_black(), 0);
}

void ui_data_update_value(int card_index, const char * value)
{
    lv_obj_t * label = NULL;
    switch(card_index) {
    case 0: label = ui_Label61; break;  /* 温度 */
    case 1: label = ui_Label64; break;
    case 2: label = ui_Label69; break;
    default: return;
    }
    if(label) lv_label_set_text(label, value);
}

void ui_data_page_init(void)
{
    /* 防止重复初始化 */
    if(g_history_tbl && lv_obj_is_valid(g_history_tbl)) return;
    if(ui_Screen4 == NULL) return;

    /* SquareLine 已建好 tab：ui_TabPage2(历史数据)、ui_TabPage1(告警记录)，不再 add_tab */

    /* ===== 历史数据(lv_table) 放进 ui_TabPage2 ===== */
    g_history_tbl = lv_table_create(ui_TabPage2);
    lv_obj_set_size(g_history_tbl, LV_PCT(100), LV_PCT(100));
    lv_table_set_column_count(g_history_tbl, 4);
    lv_table_set_col_width(g_history_tbl, 0, 105);  /* 时间 */
    lv_table_set_col_width(g_history_tbl, 1, 100);  /* 设备 */
    lv_table_set_col_width(g_history_tbl, 2, 60);   /* 类型 */
    lv_table_set_col_width(g_history_tbl, 3, 90);   /* 数值 */
    lv_table_set_cell_value(g_history_tbl, 0, 0, "时间");
    lv_table_set_cell_value(g_history_tbl, 0, 1, "设备");
    lv_table_set_cell_value(g_history_tbl, 0, 2, "类型");
    lv_table_set_cell_value(g_history_tbl, 0, 3, "数值");
    /* 明确设置表格总行数（含表头），避免越界 */
    lv_table_set_row_count(g_history_tbl, TABLE_MAX_ROWS);
    /* 表格单元格文本用中文字体（ITEMS part） */
    lv_obj_set_style_text_font(g_history_tbl, CN_FONT, LV_PART_ITEMS);
    lv_obj_set_style_text_color(g_history_tbl, lv_color_black(), LV_PART_ITEMS);
    g_data_rows = 0;

    /* ===== 告警记录(lv_list) 放进 ui_TabPage1 ===== */
    g_alarm_list = lv_list_create(ui_TabPage1);
    lv_obj_set_size(g_alarm_list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_alarm_list, lv_color_white(), 0);
    /* 列表文本用中文字体（按钮为 list 的 ITEMS part） */
    lv_obj_set_style_text_font(g_alarm_list, CN_FONT, LV_PART_ITEMS);
    lv_obj_set_style_text_color(g_alarm_list, lv_color_black(), LV_PART_ITEMS);

    /* 填充示例数据 */
    ui_data_add_history("08-21 10:00", "客厅温度", "温度", 25.3, "°C");
    ui_data_add_history("08-21 10:00", "客厅湿度", "湿度", 62.0, "%");
    ui_data_add_history("08-21 09:55", "客厅温度", "温度", 25.1, "°C");
    ui_data_add_history("08-21 09:50", "客厅温度", "温度", 24.9, "°C");
    ui_data_add_history("08-21 09:45", "风扇转速", "功耗", 120.0, "W");

    ui_data_add_alarm("08-21 09:30", "客厅温度偏高 26.5°C");
    ui_data_add_alarm("08-21 08:12", "风扇转速达到上限");
    ui_data_add_alarm("08-20 23:40", "灯光已自动关闭");
}

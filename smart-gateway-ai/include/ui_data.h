/**
 * @file ui_data.h
 * @brief 数据页(Screen4) UI 逻辑：lv_table 历史数据 + lv_chart 温度趋势 + lv_list 告警
 *
 * SquareLine 导出的 Screen4 只有静态标签和空 TabView。
 * 本模块在 ui_init() 后调用，向 TabView 填充数据控件。
 */
#ifndef UI_DATA_H
#define UI_DATA_H

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化数据页（在 ui_init() 之后调用） */
void ui_data_page_init(void);

/**
 * 新增一条历史数据（表格行：时间/设备/类型/数值）
 * @param time  时间，如 "08-21 10:00"
 * @param dev   设备名，如 "客厅温度"
 * @param type  类型，如 "温度"/"湿度"/"功耗"
 * @param value 数值
 * @param unit  单位，如 "°C"/"%"/"W"
 */
void ui_data_add_history(const char * time, const char * dev,
                         const char * type, float value, const char * unit);

/** 新增一条告警记录 */
void ui_data_add_alarm(const char * time, const char * msg);

/** 更新顶部数据卡片值（card_index: 0=温度 1=湿度 2=功耗） */
void ui_data_update_value(int card_index, const char * value);

#ifdef __cplusplus
}
#endif

#endif /* UI_DATA_H */

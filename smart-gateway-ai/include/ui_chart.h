/**
 * @file ui_chart.h
 * @brief 图表模块：Screen1 温度趋势折线图 + Screen4 迷你柱状图
 *
 * 在 ui_init() 后调用：
 *  - ui_chart_page_init()：创建 Screen1 温度趋势图 + Screen4 三张迷你柱状图
 *  - ui_chart_add_temp(v)：往温度趋势图追加一个点（自动滚动）
 */
#ifndef UI_CHART_H
#define UI_CHART_H

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化图表（在 ui_init() 之后调用） */
void ui_chart_page_init(void);

/** 追加一个温度数据点到 Screen1 趋势图 */
void ui_chart_add_temp(float temp);

/** 更新 Screen4 迷你柱状图（idx: 0=温度 1=湿度 2=功耗，val 0-100） */
void ui_chart_set_mini(int idx, int val);

#ifdef __cplusplus
}
#endif

#endif /* UI_CHART_H */

/**
 * @file rule_engine.h
 * @brief 本地自动化规则引擎
 *
 * 网关侧本地规则：无需云端参与，满足条件即触发动作。
 * 典型规则：
 *  - 温度 > 30°C -> 开风扇(1800rpm)
 *  - 温度 < 24°C -> 关风扇(0rpm)
 *  - 功耗 > 25W  -> 云端/日志告警
 *
 * 触发带冷却时间（防抖），避免传感器波动导致设备反复开关。
 */
#ifndef RULE_ENGINE_H
#define RULE_ENGINE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* 传感器类型（对齐协议 DATA_REPORT payload[0]） */
#define RULE_SENSOR_TEMP  1
#define RULE_SENSOR_HUMI  2
#define RULE_SENSOR_POWER 3

/* 比较操作 */
typedef enum {
    RULE_OP_GT,   /* 值 > 阈值时触发 */
    RULE_OP_LT,   /* 值 < 阈值时触发 */
} rule_op_t;

/* 一条规则 */
typedef struct {
    const char   * name;           /* 规则名（日志用） */
    uint8_t        sensor;         /* 关联传感器 RULE_SENSOR_* */
    rule_op_t      op;             /* 比较操作 */
    int            threshold;      /* 触发阈值 */
    uint8_t        action_cmd;     /* 动作命令（CMD_SET_FAN / CMD_SET_LIGHT ...） */
    uint32_t       action_dev;     /* 动作目标设备 dev_id */
    uint8_t        action_payload[4]; /* 动作负载（如风扇转速大端 2 字节） */
    size_t         action_plen;
    uint32_t       cooldown_ms;    /* 触发后冷却时间（防抖），0 表示每次都触发 */
} rule_t;

/* 规则触发回调：网关注入，真正下发命令 / 上报云端 */
typedef void (*rule_action_cb_t)(const rule_t * rule, void * user);

/**
 * @brief 注册规则集
 * @param rules 规则数组
 * @param n     条数
 * @param cb    触发回调（可为 NULL，仅日志）
 * @param user  回调上下文
 */
void rule_engine_configure(const rule_t * rules, size_t n,
                           rule_action_cb_t cb, void * user);

/**
 * @brief 喂入传感器数据（DATA_REPORT / 模拟数据源调用）
 * @param sensor  RULE_SENSOR_*
 * @param value   值
 * @param now_ms  当前毫秒时间戳
 */
void rule_engine_feed(uint8_t sensor, int value, uint32_t now_ms);

/**
 * @brief 内置演示规则集（温度联动风扇 + 功耗阈值）
 */
const rule_t * rule_engine_defaults(size_t * n);

#endif /* RULE_ENGINE_H */
/**
 * @file rule_engine.c
 * @brief 本地自动化规则引擎实现
 *
 * 规则格式：sensor + 比较 + 阈值 -> 命中后执行动作（发命令到目标设备）。
 * 每条规则带冷却时间（防抖），防止传感器临界值抖动导致设备频繁开关。
 */
#include <stdio.h>
#include <string.h>

#include "rule_engine.h"
#include "protocol.h"

/* ============ 静态状态 ============ */
static const rule_t *    g_rules = NULL;
static size_t            g_nrules = 0;
static rule_action_cb_t  g_cb = NULL;
static void            * g_user = NULL;

/* 每条规则上次触发时间（用于冷却判断） */
static uint32_t          g_last_trigger[16];
static uint32_t          g_trigger_count[16];

#define RULE_MAX_RULES  ((int)(sizeof(g_last_trigger) / sizeof(g_last_trigger[0])))

void rule_engine_configure(const rule_t * rules, size_t n,
                           rule_action_cb_t cb, void * user)
{
    g_rules  = rules;
    g_nrules = n < RULE_MAX_RULES ? n : RULE_MAX_RULES;
    g_cb     = cb;
    g_user   = user;
    memset(g_last_trigger, 0, sizeof(g_last_trigger));
    memset(g_trigger_count, 0, sizeof(g_trigger_count));
    printf("[rule] 配置 %d 条规则\n", (int)g_nrules);
}

static int rule_condition_met(const rule_t * r, int value)
{
    if(r->op == RULE_OP_GT) return value > r->threshold;
    return value < r->threshold;
}

void rule_engine_feed(uint8_t sensor, int value, uint32_t now_ms)
{
    for(size_t i = 0; i < g_nrules; i++) {
        const rule_t * r = &g_rules[i];
        if(r->sensor != sensor) continue;
        if(!rule_condition_met(r, value)) continue;

        /* 冷却判断：距上次触发不足 cooldown 则跳过（防抖）
         * 注意：g_last_trigger 初始为 0，首次触发时不能误判为"刚触发过" */
        if(r->cooldown_ms > 0 && g_last_trigger[i] != 0 &&
           now_ms - g_last_trigger[i] < r->cooldown_ms) {
            continue;
        }

        g_last_trigger[i]   = now_ms;
        g_trigger_count[i]++;

        printf("[rule] 触发 #%zu: %s (sensor=%d value=%d thr=%d) 次数=%u\n",
               i, r->name, sensor, value, r->threshold, g_trigger_count[i]);
        if(g_cb) g_cb(r, g_user);
    }
}

/* ============ 内置演示规则 ============
 * 温度联动风扇：>30°C 开风扇 1800rpm（0x0708），<24°C 关风扇（0x0000）
 * 功耗阈值：>25W 记录告警（动作发到模拟传感器即可，cli 里主要看日志）
 * 注意：动作 payload 按协议用大端 2 字节传数值
 */
const rule_t * rule_engine_defaults(size_t * n)
{
    static rule_t rules[] = {
        {
            .name = "高温开风扇",
            .sensor = RULE_SENSOR_TEMP,
            .op = RULE_OP_GT,
            .threshold = 30,
            .action_cmd = CMD_SET_FAN,
            .action_dev = 0x00F00002,
            .action_payload = { 0x07, 0x08 },   /* 1800 rpm 大端 */
            .action_plen = 2,
            .cooldown_ms = 15000,
        },
        {
            .name = "低温关风扇",
            .sensor = RULE_SENSOR_TEMP,
            .op = RULE_OP_LT,
            .threshold = 24,
            .action_cmd = CMD_SET_FAN,
            .action_dev = 0x00F00002,
            .action_payload = { 0x00, 0x00 },   /* 0 rpm = 关 */
            .action_plen = 2,
            .cooldown_ms = 15000,
        },
        {
            .name = "功耗高告警",
            .sensor = RULE_SENSOR_POWER,
            .op = RULE_OP_GT,
            .threshold = 25,
            .action_cmd = 0x00,                  /* 仅日志，不实际下发 */
            .action_dev = 0,
            .action_plen = 0,
            .cooldown_ms = 30000,
        },
    };
    if(n) *n = sizeof(rules) / sizeof(rules[0]);
    return rules;
}
/**
 * @file test_rule_engine.c
 * @brief 规则引擎单元测试
 *
 * 验证：
 *  1. 温度超过阈值触发"高温开风扇"且带冷却（防抖）
 *  2. 冷却期内不重复触发
 *  3. 低于阈值触发"低温关风扇"
 *  4. 不相关传感器不触发
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "rule_engine.h"
#include "protocol.h"

static int g_trigger_count = 0;
static const rule_t * g_last_rule = NULL;

static void action_cb(const rule_t * rule, void * user)
{
    (void)user;
    g_trigger_count++;
    g_last_rule = rule;
}

int main(void)
{
    size_t n = 0;
    const rule_t * rules = rule_engine_defaults(&n);
    assert(n == 3);
    rule_engine_configure(rules, n, action_cb, NULL);

    /* 1. 温度 35°C > 30 -> 触发"高温开风扇" */
    rule_engine_feed(RULE_SENSOR_TEMP, 35, 1000);
    assert(g_trigger_count == 1);
    assert(g_last_rule && g_last_rule->action_cmd == CMD_SET_FAN);
    assert(g_last_rule->action_payload[0] == 0x07 &&
           g_last_rule->action_payload[1] == 0x08);   /* 1800rpm */
    printf("[OK] 高温开风扇触发，payload=0x%02X%02X\n",
           g_last_rule->action_payload[0], g_last_rule->action_payload[1]);

    /* 2. 冷却期 15s 内再喂 36°C -> 不重复触发 */
    rule_engine_feed(RULE_SENSOR_TEMP, 36, 2000);
    assert(g_trigger_count == 1);
    printf("[OK] 冷却期内防抖生效\n");

    /* 3. 冷却结束后温度回调到 23°C < 24 -> 触发"低温关风扇" */
    rule_engine_feed(RULE_SENSOR_TEMP, 23, 17000);
    assert(g_trigger_count == 2);
    assert(g_last_rule && g_last_rule->action_payload[0] == 0x00 &&
           g_last_rule->action_payload[1] == 0x00);
    printf("[OK] 低温关风扇触发，payload=0x00 0x00\n");

    /* 4. 不相关传感器（湿度 80）不应触发任何规则 */
    rule_engine_feed(RULE_SENSOR_HUMI, 80, 18000);
    assert(g_trigger_count == 2);
    printf("[OK] 不相关传感器不触发\n");

    printf("\n全部规则引擎测试通过 (该用例由 test_rule_engine 运行)\n");
    return 0;
}
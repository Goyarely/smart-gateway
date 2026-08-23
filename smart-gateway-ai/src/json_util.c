/**
 * @file json_util.c
 * @brief 自研轻量 JSON 工具实现（参照 lv_port_linux 的 weather.c 思路）
 *
 * 不引入 cJSON，仅支持本项目的简单 JSON 场景：
 *  - 扁平对象（无嵌套）
 *  - 字段值类型：字符串 / 整数 / 浮点 / 布尔
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "json_util.h"

/* ============ 解析 ============ */

int json_get_str(const char * json, const char * key, char * out, int out_len)
{
    char key_fmt[64];
    snprintf(key_fmt, sizeof(key_fmt), "\"%s\"", key);

    const char * p = strstr(json, key_fmt);
    if(p == NULL) return -1;

    /* 跳过字段名，往后找值的左引号 */
    p = strstr(p + strlen(key_fmt), "\"");
    if(p == NULL) return -1;
    p++;

    /* 读到右引号 */
    const char * end = strchr(p, '"');
    if(end == NULL) return -1;

    int len = (int)(end - p);
    if(len >= out_len) len = out_len - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 0;
}

int json_get_int(const char * json, const char * key, int * out)
{
    char key_fmt[64];
    snprintf(key_fmt, sizeof(key_fmt), "\"%s\":", key);

    const char * p = strstr(json, key_fmt);
    if(p == NULL) return -1;

    p += strlen(key_fmt);
    while(*p == ' ' || *p == '\t') p++;

    /* p 指向负号或数字起始，atoi 可正确处理前导负号 */
    const char * num = p;
    if(*num == '-') num++;
    if(*num < '0' || *num > '9') return -1;

    *out = atoi(p);
    return 0;
}

int json_get_double(const char * json, const char * key, double * out)
{
    char key_fmt[64];
    snprintf(key_fmt, sizeof(key_fmt), "\"%s\":", key);

    const char * p = strstr(json, key_fmt);
    if(p == NULL) return -1;

    p += strlen(key_fmt);
    while(*p == ' ' || *p == '\t') p++;

    if(!((*p >= '0' && *p <= '9') || *p == '-' || *p == '.')) return -1;

    *out = strtod(p, NULL);
    return 0;
}

int json_get_bool(const char * json, const char * key, bool * out)
{
    char key_fmt[64];
    snprintf(key_fmt, sizeof(key_fmt), "\"%s\":", key);

    const char * p = strstr(json, key_fmt);
    if(p == NULL) return -1;

    p += strlen(key_fmt);
    while(*p == ' ' || *p == '\t') p++;

    if(strncmp(p, "true", 4) == 0) { *out = true;  return 0; }
    if(strncmp(p, "false", 5) == 0) { *out = false; return 0; }
    if(strncmp(p, "1", 1) == 0)   { *out = true;  return 0; }
    if(strncmp(p, "0", 1) == 0)   { *out = false; return 0; }
    return -1;
}

/* ============ 构建 ============ */

void json_build_begin(json_build_t * jb, char * buf, int size)
{
    jb->buf   = buf;
    jb->size  = size;
    jb->len   = 0;
    jb->first = true;
    jb->done  = false;
    if(size > 0) {
        buf[0] = '{';
        jb->len = 1;
    }
}

static int json_build_reserve(json_build_t * jb, int need)
{
    /* 需要预留结尾的 '}' 和 '\0' */
    if(jb->done || jb->len + need + 2 > jb->size) return -1;
    return 0;
}

static void json_build_sep(json_build_t * jb)
{
    if(jb->first) {
        jb->first = false;
    }
    else {
        if(jb->len + 1 < jb->size) {
            jb->buf[jb->len++] = ',';
        }
    }
}

static int json_build_key(json_build_t * jb, const char * key)
{
    int n = snprintf(jb->buf + jb->len, jb->size - jb->len, "\"%s\":", key);
    if(n < 0) return -1;
    jb->len += n;
    return 0;
}

void json_build_add_str(json_build_t * jb, const char * key, const char * val)
{
    if(json_build_reserve(jb, (int)strlen(key) + (int)strlen(val) + 6) != 0) return;
    json_build_sep(jb);
    if(json_build_key(jb, key) != 0) return;
    int n = snprintf(jb->buf + jb->len, jb->size - jb->len, "\"%s\"", val ? val : "");
    if(n > 0) jb->len += n;
}

void json_build_add_int(json_build_t * jb, const char * key, long val)
{
    if(json_build_reserve(jb, (int)strlen(key) + 24) != 0) return;
    json_build_sep(jb);
    if(json_build_key(jb, key) != 0) return;
    int n = snprintf(jb->buf + jb->len, jb->size - jb->len, "%ld", val);
    if(n > 0) jb->len += n;
}

void json_build_add_double(json_build_t * jb, const char * key, double val)
{
    if(json_build_reserve(jb, (int)strlen(key) + 32) != 0) return;
    json_build_sep(jb);
    if(json_build_key(jb, key) != 0) return;
    int n = snprintf(jb->buf + jb->len, jb->size - jb->len, "%.2f", val);
    if(n > 0) jb->len += n;
}

void json_build_add_bool(json_build_t * jb, const char * key, bool val)
{
    if(json_build_reserve(jb, (int)strlen(key) + 8) != 0) return;
    json_build_sep(jb);
    if(json_build_key(jb, key) != 0) return;
    const char * s = val ? "true" : "false";
    int n = snprintf(jb->buf + jb->len, jb->size - jb->len, "%s", s);
    if(n > 0) jb->len += n;
}

int json_build_end(json_build_t * jb)
{
    if(jb->done) return -1;
    if(jb->len + 2 > jb->size) return -1;
    jb->buf[jb->len++] = '}';
    jb->buf[jb->len] = '\0';
    jb->done = true;
    return 0;
}

const char * json_build_str(json_build_t * jb)
{
    return jb->buf;
}

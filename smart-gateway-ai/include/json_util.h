/**
 * @file json_util.h
 * @brief 自研轻量 JSON 工具（方案 A，不引入 cJSON）
 *
 * 适用场景：字段固定、结构简单的 JSON（MQTT 数据上报、天气 API 等）。
 * 特点：无外部依赖，体积小，够用即可。
 *
 * 两个方向：
 *   1. 解析：从 JSON 字符串里按 key 取值（json_get_*）
 *   2. 构建：用固定缓冲区拼一个 JSON 对象（json_build_*）
 */
#ifndef JSON_UTIL_H
#define JSON_UTIL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============ 解析 ============ */

/**
 * 取字符串字段值（如 {"city":"广州"} 取 city -> "广州"）
 * @param json     JSON 文本
 * @param key      字段名（不含引号）
 * @param out      输出缓冲区
 * @param out_len  输出缓冲区大小
 * @return 0 成功，-1 失败
 */
int json_get_str(const char * json, const char * key, char * out, int out_len);

/**
 * 取整数字段值（如 {"temperature":34} 取 temperature -> 34）
 */
int json_get_int(const char * json, const char * key, int * out);

/**
 * 取浮点字段值（如 {"humidity":62.5} 取 humidity -> 62.5）
 */
int json_get_double(const char * json, const char * key, double * out);

/**
 * 取布尔字段值（如 {"online":true} 取 online -> true）
 */
int json_get_bool(const char * json, const char * key, bool * out);

/* ============ 构建 ============ */

/**
 * 构建器。用法：
 *   char buf[256]; json_build_t jb;
 *   json_build_begin(&jb, buf, sizeof(buf));
 *   json_build_add_str(&jb, "city", "广州");
 *   json_build_add_int(&jb, "temp", 34);
 *   json_build_add_double(&jb, "hum", 62.5);
 *   json_build_add_bool(&jb, "online", true);
 *   json_build_end(&jb);   // 得到 {"city":"广州","temp":34,...}
 */
typedef struct {
    char * buf;
    int    size;
    int    len;   /* 当前已写入字节数 */
    bool   first; /* 是否第一个字段（决定要不要逗号） */
    bool   done;  /* 是否已 end */
} json_build_t;

/** 开始构建，清空缓冲区 */
void json_build_begin(json_build_t * jb, char * buf, int size);

/** 追加字符串字段 */
void json_build_add_str(json_build_t * jb, const char * key, const char * val);

/** 追加整数字段 */
void json_build_add_int(json_build_t * jb, const char * key, long val);

/** 追加浮点字段 */
void json_build_add_double(json_build_t * jb, const char * key, double val);

/** 追加布尔字段 */
void json_build_add_bool(json_build_t * jb, const char * key, bool val);

/** 结束构建，补上 '}'。返回 0 成功，-1 缓冲区不足/已结束 */
int json_build_end(json_build_t * jb);

/** 取构建完成的 JSON 字符串 */
const char * json_build_str(json_build_t * jb);

#ifdef __cplusplus
}
#endif

#endif /* JSON_UTIL_H */

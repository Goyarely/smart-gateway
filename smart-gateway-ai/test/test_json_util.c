/**
 * @file test_json_util.c
 * 自研轻量 JSON 工具自测。
 * 编译：见 CMakeLists.txt 的 test_json_util 目标
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "json_util.h"

static int pass = 0;
static int fail = 0;

#define CHECK(cond) do { \
    if(cond) { pass++; } \
    else { fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while(0)

int main(void)
{
    /* ===== 解析 ===== */
    const char * json = "{\"city\":\"广州\",\"temperature\":34,"
                        "\"humidity\":62.5,\"online\":true,\"count\":-5}";

    char s[32];
    CHECK(json_get_str(json, "city", s, sizeof(s)) == 0);
    CHECK(strcmp(s, "广州") == 0);

    int iv = 0;
    CHECK(json_get_int(json, "temperature", &iv) == 0);
    CHECK(iv == 34);
    CHECK(json_get_int(json, "count", &iv) == 0);
    CHECK(iv == -5);

    double dv = 0;
    CHECK(json_get_double(json, "humidity", &dv) == 0);
    CHECK(dv > 62.4 && dv < 62.6);

    bool b = false;
    CHECK(json_get_bool(json, "online", &b) == 0);
    CHECK(b == true);

    /* 取不存在的 key 应失败 */
    CHECK(json_get_str(json, "nope", s, sizeof(s)) == -1);

    /* ===== 构建 ===== */
    char buf[256];
    json_build_t jb;
    json_build_begin(&jb, buf, sizeof(buf));
    json_build_add_str(&jb, "city", "广州");
    json_build_add_int(&jb, "temp", 34);
    json_build_add_double(&jb, "hum", 62.5);
    json_build_add_bool(&jb, "online", true);
    CHECK(json_build_end(&jb) == 0);

    const char * built = json_build_str(&jb);
    printf("构建结果: %s\n", built);
    CHECK(strcmp(built,
        "{\"city\":\"广州\",\"temp\":34,\"hum\":62.50,\"online\":true}") == 0);

    /* 空对象 */
    json_build_begin(&jb, buf, sizeof(buf));
    CHECK(json_build_end(&jb) == 0);
    CHECK(strcmp(json_build_str(&jb), "{}") == 0);

    printf("\n结果: %d 通过, %d 失败\n", pass, fail);
    return fail == 0 ? 0 : 1;
}

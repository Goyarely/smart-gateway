/**
 * @file device.h
 * @brief 设备表：哈希表 + 心跳保活 + 超时踢出
 *
 * 每个已注册设备（dev_id -> socket fd）维护一个心跳时间。
 * 网关周期扫描，超过窗口无心跳的设备被踢出（device_remove_if_timeout）。
 */
#ifndef DEVICE_H
#define DEVICE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 设备心跳超时窗口（毫秒）：超过则踢出 */
#define DEVICE_TIMEOUT_MS  5000

/* 设备注册上限 */
#define DEVICE_MAX         64

/* 设备条目 */
typedef struct {
    uint32_t dev_id;      /* 设备 ID（帧里的 dev_id） */
    int      fd;          /* 对应 socket */
    bool     used;        /* 条目是否被占用 */
    uint64_t last_seen;   /* 最近一次心跳/收包时间（单调时钟 ms） */
} device_t;

/** 获取单调时钟（毫秒），用于心跳超时判断 */
uint64_t device_now_ms(void);

/** 初始化设备表 */
void device_init(void);

/**
 * 注册/更新设备：把 dev_id 绑定到 fd。
 * @param dev_id 设备 ID
 * @param fd     socket
 * @param now    当前单调时间 ms
 */
void device_register(uint32_t dev_id, int fd, uint64_t now);

/** 设备收包（心跳/数据）：刷新 last_seen */
void device_touch(uint32_t dev_id, uint64_t now);

/**
 * 设备断开：按 fd 移除（客户端断开时调用）
 * @param fd socket
 */
void device_remove_fd(int fd);

/**
 * 移除超时设备，返回被移除的 socket fd 数组。
 * 用于 epoll 主循环空闲时清理。
 * @param now     当前单调时间
 * @param dead_fd 输出：超时设备的 fd 数组
 * @param max     数组容量
 * @return 移除的设备数
 */
int device_remove_if_timeout(uint64_t now, int * dead_fd, int max);

/** 按 dev_id 查找设备，返回设备指针（无则 NULL） */
device_t * device_find(uint32_t dev_id);

/** 当前已注册设备数 */
int device_count(void);

/** 遍历设备（回调），返回匹配数；cb 返回 false 停止 */
typedef bool (*device_cb_t)(device_t * dev, void * user);
int device_foreach(device_cb_t cb, void * user);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_H */

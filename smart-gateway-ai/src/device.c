/**
 * @file device.c
 * @brief 设备表实现：哈希表 + 心跳保活 + 超时踢出
 *
 * 简单哈希：dev_id 直接映射到 table[dev_id % DEVICE_MAX]。
 * 冲突时线性探测。DEVICE_MAX=64，设备少，够用。
 */
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include "device.h"

static device_t g_devs[DEVICE_MAX];

/** 获取单调时钟（毫秒），用于心跳超时判断 */
uint64_t device_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

void device_init(void)
{
    memset(g_devs, 0, sizeof(g_devs));
}

static device_t * find_slot(uint32_t dev_id)
{
    uint32_t idx = dev_id % DEVICE_MAX;
    for(int i = 0; i < DEVICE_MAX; i++) {
        device_t * d = &g_devs[(idx + i) % DEVICE_MAX];
        if(d->used && d->dev_id == dev_id) return d;
    }
    return NULL;
}

/* 找一个空位 */
static device_t * find_empty_slot(void)
{
    for(int i = 0; i < DEVICE_MAX; i++) {
        if(!g_devs[i].used) return &g_devs[i];
    }
    return NULL;
}

void device_register(uint32_t dev_id, int fd, uint64_t now)
{
    device_t * d = find_slot(dev_id);
    if(d == NULL) d = find_empty_slot();
    if(d == NULL) {
        fprintf(stderr, "[device] 设备表满，无法注册 0x%08X\n", dev_id);
        return;
    }
    /* 同一 dev_id 换 fd 注册（如重连）：关闭旧 fd，避免泄漏 */
    if(d->used && d->fd != fd) {
        fprintf(stdout, "[device] dev 0x%08X 换 fd %d -> %d（关闭旧连接）\n",
                dev_id, d->fd, fd);
        close(d->fd);
    }
    d->dev_id    = dev_id;
    d->fd        = fd;
    d->used      = true;
    d->last_seen = now;
    fprintf(stdout, "[device] 注册 0x%08X fd=%d\n", dev_id, fd);
}

void device_touch(uint32_t dev_id, uint64_t now)
{
    device_t * d = find_slot(dev_id);
    if(d) d->last_seen = now;
}

void device_remove_fd(int fd)
{
    for(int i = 0; i < DEVICE_MAX; i++) {
        if(g_devs[i].used && g_devs[i].fd == fd) {
            fprintf(stdout, "[device] 断开 0x%08X fd=%d\n", g_devs[i].dev_id, fd);
            g_devs[i].used = false;
            return;
        }
    }
}

int device_remove_if_timeout(uint64_t now, int * dead_fd, int max)
{
    int cnt = 0;
    for(int i = 0; i < DEVICE_MAX; i++) {
        device_t * d = &g_devs[i];
        if(d->used && (now - d->last_seen) > DEVICE_TIMEOUT_MS) {
            if(cnt < max) dead_fd[cnt] = d->fd;
            cnt++;
            fprintf(stdout, "[device] 超时踢出 0x%08X fd=%d\n", d->dev_id, d->fd);
            d->used = false;
        }
    }
    return cnt;
}

device_t * device_find(uint32_t dev_id)
{
    return find_slot(dev_id);
}

int device_count(void)
{
    int cnt = 0;
    for(int i = 0; i < DEVICE_MAX; i++) if(g_devs[i].used) cnt++;
    return cnt;
}

int device_foreach(device_cb_t cb, void * user)
{
    int cnt = 0;
    for(int i = 0; i < DEVICE_MAX; i++) {
        if(g_devs[i].used) {
            cnt++;
            if(cb && !cb(&g_devs[i], user)) break;
        }
    }
    return cnt;
}

/**
 * @file net.h
 * @brief epoll 网关服务器接口
 *
 * 职责：TCP 服务器 + 设备接入 + 命令路由 + 超时踢出。
 * 参考《项目开发路线与代码框架.md》：epoll 阻塞主循环，event.cnt==0 时空闲清理超时设备。
 */
#ifndef NET_H
#define NET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 启动网关服务器（阻塞运行） */
int net_server_start(uint16_t port);

/** 网关版本信息（打印用） */
void net_server_print_info(void);

#ifdef __cplusplus
}
#endif

#endif /* NET_H */

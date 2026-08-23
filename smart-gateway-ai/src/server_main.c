/**
 * @file server_main.c
 * 网关 server 入口（进程 A，运行在 PC 上）
 *
 * 用法：./server [port]    默认端口 8888
 *
 * 部署：本机 PC 跑 server，开发板跑 lvglsim 面板，通过 TCP:8888 连接。
 */
#include <stdio.h>
#include <stdlib.h>
#include "net.h"

int main(int argc, char ** argv)
{
    int port = 8888;
    if(argc > 1) port = atoi(argv[1]);

    net_server_print_info();
    net_server_start((uint16_t)port);
    return 0;
}

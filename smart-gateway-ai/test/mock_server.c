/**
 * @file mock_server.c
 * 最小模拟网关，用于端到端验证 lvglsim 的 ui_bridge。
 *
 * 行为：
 *   - 监听 TCP 8888
 *   - 收到 REGISTER   -> 回 REGISTER_ACK(result=0)
 *   - 收到 HEARTBEAT  -> 保持连接，打印
 *   - 收到 SET_LIGHT/FAN/SCENE -> 打印命令值，回 SET_ACK(result=0)
 *   - 收到 DATA_REPORT-> 打印 sensor/value
 *
 * 编译：见 CMakeLists.txt 的 mock_server 目标
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "protocol.h"

static const char * cmd_name(uint8_t type)
{
    switch(type) {
    case CMD_REGISTER:     return "REGISTER";
    case CMD_REGISTER_ACK: return "REGISTER_ACK";
    case CMD_HEARTBEAT:    return "HEARTBEAT";
    case CMD_SET_LIGHT:    return "SET_LIGHT";
    case CMD_SET_FAN:      return "SET_FAN";
    case CMD_SET_SCENE:    return "SET_SCENE";
    case CMD_DATA_REPORT:  return "DATA_REPORT";
    case CMD_SET_ACK:      return "SET_ACK";
    default:               return "UNKNOWN";
    }
}

static void handle_frame(const uint8_t * data, size_t len, int fd)
{
    uint8_t  type; uint32_t dev; uint16_t seq;
    const uint8_t * payload; size_t plen;
    if(frame_parse(data, len, &type, &dev, &seq, &payload, &plen) != 0) {
        printf("[mock] 帧校验失败\n");
        return;
    }

    printf("[mock] dev=0x%08X seq=%u %s len=%zu",
           dev, seq, cmd_name(type), plen);
    if(plen) {
        printf("  payload:");
        for(size_t i = 0; i < plen; i++) printf(" %02X", payload[i]);
        /* 打印可读值 */
        switch(type) {
        case CMD_SET_LIGHT: printf("  (亮度=%d)", payload[0]); break;
        case CMD_SET_FAN: {
            int rpm = (payload[0] << 8) | payload[1];
            printf("  (转速=%d)", rpm); break;
        }
        case CMD_SET_SCENE: printf("  (场景=%d)", payload[0]); break;
        case CMD_DATA_REPORT: printf("  (sensor=%d value=%d)", payload[0], payload[1]); break;
        default: break;
        }
    }
    printf("\n");

    /* 应答 */
    uint8_t txbuf[FRAME_MAX_LEN];
    if(type == CMD_REGISTER) {
        uint8_t result = 0;
        int n = frame_pack(txbuf, CMD_REGISTER_ACK, dev, seq, &result, 1);
        send(fd, txbuf, n, 0);
        printf("[mock] 已注册，回 REGISTER_ACK\n");
    }
    else if(type == CMD_SET_LIGHT || type == CMD_SET_FAN || type == CMD_SET_SCENE) {
        uint8_t result = 0;
        int n = frame_pack(txbuf, CMD_SET_ACK, dev, seq, &result, 1);
        send(fd, txbuf, n, 0);
    }
}

int main(void)
{
    setbuf(stdout, NULL);   /* 禁用缓冲，日志实时写入（重定向到文件也可见） */
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(8888);
    if(bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if(listen(lfd, 5) < 0) { perror("listen"); return 1; }
    printf("[mock] 模拟网关监听 8888...\n");

    while(1) {
        int fd = accept(lfd, NULL, NULL);
        if(fd < 0) { perror("accept"); continue; }
        printf("[mock] 面板连入 fd=%d\n", fd);

        /* 收帧拆帧（单连接阻塞） */
        uint8_t rx[FRAME_MAX_LEN * 2];
        size_t rxlen = 0;
        while(1) {
            ssize_t r = recv(fd, rx + rxlen, sizeof(rx) - rxlen, 0);
            if(r <= 0) { printf("[mock] 面板断开 fd=%d\n", fd); close(fd); break; }
            rxlen += (size_t)r;

            /* 拆帧 */
            while(rxlen >= FRAME_HEAD_LEN + FRAME_CRC_LEN) {
                if(rd_be16(rx) != FRAME_MAGIC) {
                    memmove(rx, rx + 1, rxlen - 1);
                    rxlen--; continue;
                }
                uint16_t len = rd_be16(rx + 3);
                size_t total = (size_t)len + FRAME_CRC_LEN;
                if(total > rxlen) break;
                handle_frame(rx, total, fd);
                memmove(rx, rx + total, rxlen - total);
                rxlen -= total;
            }
        }
    }
    return 0;
}

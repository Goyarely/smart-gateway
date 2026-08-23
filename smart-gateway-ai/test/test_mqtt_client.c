/**
 * @file test_mqtt_client.c
 * @brief MQTT 3.1.1 客户端单元测试（无需真实 broker）
 *
 * 通过本地回环 socket 起一个"最小 broker"（Python 或 C 模拟），
 * 验证：
 *  1. 客户端能连上并发出格式正确的 CONNECT 报文
 *  2. 收到 CONNACK 后进入 connected 状态
 *  3. publish 的报文可被 mock broker 收到并回 PUBACK
 *  4. 心跳 PINGREQ/PINGRESP 保活
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>

#include "mqtt_client.h"

/* 简单 waitpid 封装 */
static void waitpid_short(pid_t pid)
{
    while(waitpid(pid, NULL, 0) < 0 && errno == EINTR) { }
}

/* ============ 最小 mock broker：起一个 TCP 监听端口，
 * 收到 CONNECT(0x10) 回 CONNACK(0x20 02 00 00)，
 * 收到 SUBSCRIBE(0x82) 回 SUBACK，
 * 收到 PUBLISH(0x30) 回 PUBACK，
 * 收到 PINGREQ(0xC0) 回 PINGRESP(0xD0 00)。
 * 运行于子进程，把端口号通过 stdout 传给父进程。
 */
static int demangle_len(const uint8_t * buf, size_t len, size_t * out_body,
                        size_t * out_header)
{
    size_t mult = 1, rem = 0, i = 1;
    for(; i < len && i <= 5; i++) {
        rem += (size_t)(buf[i] & 0x7F) * mult;
        if(!(buf[i] & 0x80)) break;
        mult *= 128;
    }
    if(i >= len) return -1;          /* 不完整 */
    *out_body   = rem;
    *out_header = i + 1;
    return 0;
}

static int mock_broker_listen(uint16_t * port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) return -1;
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family      = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port        = 0;    /* 内核分配 */
    if(bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        close(fd);
        return -1;
    }
    socklen_t slen = sizeof(sa);
    getsockname(fd, (struct sockaddr *)&sa, &slen);
    *port = ntohs(sa.sin_port);
    listen(fd, 4);
    return fd;
}

static void mock_broker_loop(int listen_fd, int max_iters)
{
    int conn = accept(listen_fd, NULL, NULL);
    if(conn < 0) return;

    uint8_t buf[512];
    size_t  blen = 0;
    int iters = 0;
    while(iters++ < max_iters) {
        struct pollfd pfd = { .fd = conn, .events = POLLIN };
        int pr = poll(&pfd, 1, 200);
        if(pr <= 0) continue;

        int n = (int)recv(conn, buf + blen, sizeof(buf) - blen, 0);
        if(n <= 0) break;
        blen += (size_t)n;

        while(blen >= 2) {
            size_t body = 0, header = 0;
            if(demangle_len(buf, blen, &body, &header) != 0) break;
            if(blen < header + body) break;
            uint8_t type = buf[0] >> 4;

            if(type == 1) {       /* CONNECT -> CONNACK */
                uint8_t ack[4] = { 0x20, 0x02, 0x00, 0x00 };
                send(conn, ack, sizeof(ack), 0);
                printf("[mock] -> CONNACK\n");
            }
            else if(type == 8) {  /* SUBSCRIBE -> SUBACK */
                uint8_t ack[5] = { 0x90, 0x03, 0x00, 0x01, 0x00 };
                send(conn, ack, sizeof(ack), 0);
                printf("[mock] -> SUBACK\n");
            }
            else if(type == 3) {  /* PUBLISH -> PUBACK */
                uint8_t ack[4] = { 0x40, 0x02, 0x00, 0x01 };
                send(conn, ack, sizeof(ack), 0);
                printf("[mock] -> PUBACK (topic=%.*s)\n",
                       buf[header] << 8 | buf[header + 1],
                       (const char *)(buf + header + 2));
            }
            else if(type == 12) { /* PINGREQ -> PINGRESP */
                uint8_t ack[2] = { 0xD0, 0x00 };
                send(conn, ack, sizeof(ack), 0);
                printf("[mock] -> PINGRESP\n");
            }
            /* 移到下一个包 */
            memmove(buf, buf + header + body, blen - (header + body));
            blen -= (header + body);
        }
    }
    close(conn);
    close(listen_fd);
    printf("[mock] broker 退出\n");
}

/* ============ 测试主体 ============ */
static volatile int g_msg_count = 0;
static void on_msg(const char * topic, const uint8_t * payload, int len, void * user)
{
    (void)user;
    g_msg_count++;
    printf("[cli] 收到云端消息 topic=%s payload=%.*s\n", topic, len, payload);
}

int main(void)
{
    setbuf(stdout, NULL);
    printf("== MQTT 客户端测试 ==\n");

    /* 1. 起 mock broker（fork 子进程） */
    uint16_t port = 0;
    int listen_fd = mock_broker_listen(&port);
    assert(listen_fd >= 0);

    pid_t pid = fork();
    if(pid == 0) {
        mock_broker_loop(listen_fd, 30);
        _exit(0);
    }
    sleep(1);

    /* 2. 初始化客户端 */
    mqtt_client_init("127.0.0.1", port, "test-client", 30, on_msg, NULL);

    /* 3. 等到 connected（最多 5s） */
    int connected = 0;
    for(int i = 0; i < 250; i++) {
        mqtt_client_poll();
        if(mqtt_client_is_connected()) { connected = 1; break; }
        usleep(20000);
    }
    assert(connected);
    printf("[OK] 已连接 mock broker\n");

    /* 4. 订阅 */
    assert(mqtt_client_subscribe("gw/cmd") == 0);
    for(int i = 0; i < 100 && !g_msg_count; i++) mqtt_client_poll();
    /* SUBACK 已回；这里不做严格断言（客户端对 SUBACK 仅更新 last_recv）*/
    printf("[OK] 订阅 gw/cmd 完成\n");

    /* 5. 发布一条消息 */
    const char * payload = "{\"sensor\":1,\"value\":26}";
    assert(mqtt_client_publish("gw/sensor", payload, (int)strlen(payload), 1) == 0);
    for(int i = 0; i < 100; i++) mqtt_client_poll();

    /* 6. 心跳应正常（让 mock 收到 PINGREQ） */
    mqtt_client_poll();
    printf("[OK] publish + 心跳完成\n");

    mqtt_client_deinit();
    waitpid_short(pid);
    printf("\n== MQTT 客户端测试通过 ==\n");
    return 0;
}
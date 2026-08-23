/**
 * @file mqtt_client.c
 * @brief 自研 MQTT 3.1.1 客户端实现
 *
 * 协议要点：
 *  - 固定头：首字节 [类型<<4 | flags]，后跟剩余长度（变长编码 ≤4 字节）
 *  - CONNECT 可变头：协议名 "MQTT"、级别 4、连接标志、KeepAlive(2B)
 *  - PUBLISH QoS1 需要 PACKET ID，等待 PUBACK
 *  - SUBSCRIBE 带 PACKET ID 与主题，等 SUBACK
 *  - 心跳：每 keepalive/2 秒发 PINGREQ，等 PINGRESP
 *
 * 状态机：DISCONNECTED -> TCP_CONNECTING -> MQTT_CONNECTING -> CONNECTED
 * 断线后指数退避重连（1s 起，最多 30s）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>

#include "mqtt_client.h"

/* ============ 报文类型 ============ */
#define MQTT_CONNECT     1
#define MQTT_CONNACK     2
#define MQTT_PUBLISH     3
#define MQTT_PUBACK      4
#define MQTT_SUBSCRIBE   8
#define MQTT_SUBACK      9
#define MQTT_PINGREQ    12
#define MQTT_PINGRESP   13
#define MQTT_DISCONNECT 14

#define MQTT_PROTO_LEVEL 4   /* 3.1.1 */

/* keepalive 默认 30s，心跳间隔 = keepalive / 2 */
#define KEEPALIVE_DEF       30
#define RECONNECT_MIN_MS    1000
#define RECONNECT_MAX_MS    30000

/* ============ 静态状态 ============ */
static int      g_sock = -1;
static bool     g_enabled = false;      /* 是否已初始化（未初始化不连接） */
static bool     g_connecting = false;   /* TCP 连接中或 MQTT 握手未完成 */
static bool     g_connected  = false;
static uint16_t g_keepalive  = KEEPALIVE_DEF;
static char     g_client_id[64] = "smart-gateway-ai";
static char     g_host[64] = "";
static uint16_t g_port     = 1883;

static mqtt_msg_cb_t g_msg_cb = NULL;
static void        * g_user   = NULL;

static uint8_t  g_tx[512];
static uint8_t  g_rx[MQTT_RXBUF_MAX];
static size_t   g_rxlen = 0;

static uint16_t g_pktid = 1;           /* PACKET ID 分配 */

static int64_t  g_reconnect_ms = RECONNECT_MIN_MS;
static int64_t  g_last_recv_ms = 0;    /* 最后收到任何包的时间 */
static int64_t  g_last_ping_ms = 0;    /* 上次发 PINGREQ 时间 */
static int64_t  g_last_connect_ms = 0; /* 上次发起 TCP 连接时间 */
static bool     g_send_connect = false;

/* 待订阅主题（连接建立后自动补订） */
static char     g_sub_topic[MQTT_TOPIC_MAX] = "";

/* ============ 时间工具 ============ */
static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ============ 剩余长度编码 ============ */
static int encode_remaining_len(uint8_t * dst, size_t len)
{
    int n = 0;
    do {
        uint8_t b = (uint8_t)(len % 128);
        len /= 128;
        if(len > 0) b |= 0x80;
        dst[n++] = b;
    } while(len > 0 && n < 4);
    return n;
}

/* ============ 报文构造 ============ */
static int build_connect(uint8_t * dst)
{
    /* 可变头 + payload 构建到临时缓冲 */
    uint8_t vh[256];
    size_t pos = 0;

    /* 协议名 "MQTT"（2 字节长度 + 4 字节） */
    vh[pos++] = 0x00; vh[pos++] = 0x04;
    vh[pos++] = 'M';  vh[pos++] = 'Q'; vh[pos++] = 'T'; vh[pos++] = 'T';
    /* 级别 4 = MQTT 3.1.1 */
    vh[pos++] = MQTT_PROTO_LEVEL;
    /* 连接标志：clean session = 0x02（用户名/密码不启用） */
    vh[pos++] = 0x02;
    /* keepalive */
    vh[pos++] = (uint8_t)(g_keepalive >> 8);
    vh[pos++] = (uint8_t)(g_keepalive & 0xFF);

    /* client id */
    size_t clen = strlen(g_client_id);
    vh[pos++] = (uint8_t)(clen >> 8);
    vh[pos++] = (uint8_t)(clen & 0xFF);
    memcpy(vh + pos, g_client_id, clen);
    pos += clen;

    dst[0] = (uint8_t)(MQTT_CONNECT << 4);
    int rl = encode_remaining_len(dst + 1, pos);
    memcpy(dst + 1 + rl, vh, pos);
    return 1 + rl + (int)pos;
}

static int build_publish(uint8_t * dst, const char * topic,
                         const void * payload, int len, uint8_t qos)
{
    size_t tlen = strlen(topic);
    size_t total = 2 + tlen;               /* topic len + topic */
    if(qos > 0) total += 2;                /* packet id */
    total += (size_t)len;                  /* payload */

    size_t pos = 0;
    uint8_t flags = (uint8_t)((qos & 0x03) << 1);   /* DUP=0, QoS, RETAIN=0 */
    dst[pos++] = (uint8_t)((MQTT_PUBLISH << 4) | flags);
    pos += (size_t)encode_remaining_len(dst + pos, total);

    dst[pos++] = (uint8_t)(tlen >> 8);
    dst[pos++] = (uint8_t)(tlen & 0xFF);
    memcpy(dst + pos, topic, tlen);
    pos += tlen;

    if(qos > 0) {
        dst[pos++] = (uint8_t)(g_pktid >> 8);
        dst[pos++] = (uint8_t)(g_pktid & 0xFF);
        g_pktid++;
    }
    memcpy(dst + pos, payload, (size_t)len);
    pos += (size_t)len;
    return (int)pos;
}

static int build_subscribe(uint8_t * dst, const char * topic)
{
    size_t tlen = strlen(topic);
    size_t total = 2 + 2 + tlen + 1;   /* packet id + topic len + topic + qos */

    size_t pos = 0;
    dst[pos++] = (uint8_t)((MQTT_SUBSCRIBE << 4) | 0x02);  /* flags=0x02 必填 */
    pos += (size_t)encode_remaining_len(dst + pos, total);

    dst[pos++] = (uint8_t)(g_pktid >> 8);
    dst[pos++] = (uint8_t)(g_pktid & 0xFF);
    g_pktid++;

    dst[pos++] = (uint8_t)(tlen >> 8);
    dst[pos++] = (uint8_t)(tlen & 0xFF);
    memcpy(dst + pos, topic, tlen);
    pos += tlen;
    dst[pos++] = 0x00;   /* QoS0 */
    return (int)pos;
}

/* ============ socket 工具 ============ */
static void sock_close(void)
{
    if(g_sock >= 0) {
        close(g_sock);
        g_sock = -1;
    }
    g_connected  = false;
    g_connecting = false;
    g_rxlen      = 0;
    g_send_connect = false;
}

static int sock_connect(void)
{
    sock_close();

    g_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(g_sock < 0) return -1;

    int flags = fcntl(g_sock, F_GETFL, 0);
    fcntl(g_sock, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(g_port);
    if(inet_pton(AF_INET, g_host, &sa.sin_addr) != 1) {
        sock_close();
        return -1;
    }

    int ret = connect(g_sock, (struct sockaddr *)&sa, sizeof(sa));
    if(ret != 0 && errno != EINPROGRESS) {
        printf("[mqtt] connect %s:%u 失败: %s\n", g_host, g_port, strerror(errno));
        sock_close();
        return -1;
    }
    g_connecting     = true;
    g_send_connect   = true;
    return 0;
}

/* 发送缓冲区 */
static int sock_send(const uint8_t * buf, int len)
{
    int wr = (int)send(g_sock, buf, (size_t)len, MSG_NOSIGNAL);
    if(wr <= 0) return -1;
    return wr;
}

/* ============ 收包解析 ============ */
static void handle_publish_frame(const uint8_t * data, size_t len)
{
    /* data 起始于固定头之后（含 topic len / topic / payload） */
    size_t pos = 0;
    if(len < 2) return;
    uint16_t tlen = (uint16_t)((data[0] << 8) | data[1]);
    pos = 2;
    if(pos + tlen > len) return;

    char topic[MQTT_TOPIC_MAX];
    snprintf(topic, sizeof(topic), "%.*s", tlen, data + pos);
    pos += tlen;

    /* QoS1 时跳过 packet id（固定头低 2 位） */
    uint8_t first = data[0];
    (void)first;
    const uint8_t * payload = data + pos;
    int plen = (int)(len - pos);
    if(g_msg_cb) g_msg_cb(topic, payload, plen, g_user);
}

static int parse_fixed_header(const uint8_t * d, size_t len,
                              uint8_t * out_type, size_t * out_body)
{
    /* 解析剩余长度（变长编码），返回固定头长度 */
    if(len < 2) return 0;                 /* 数据不足 */
    uint8_t type = d[0] >> 4;
    *out_type = type;

    size_t mult = 1, rem = 0;
    size_t i = 1;
    for(; i < len && i <= 5; i++) {
        rem += (size_t)(d[i] & 0x7F) * mult;
        if(!(d[i] & 0x80)) break;
        mult *= 128;
    }
    if(i >= 5 && (d[4] & 0x80)) return -1;  /* 剩余长度非法 */
    if(i >= len) return 0;                  /* 剩余长度编码未读完 */
    *out_body = rem;
    return (int)i;                          /* 固定头总长 */
}

/* 处理 TCP 流缓冲里的完整 MQTT 包 */
static void mqtt_process_rx(void)
{
    for(;;) {
        if(g_rxlen < 2) break;
        uint8_t type = 0;
        size_t  body = 0;
        int hlen = parse_fixed_header(g_rx, g_rxlen, &type, &body);
        if(hlen == 0) break;               /* 不完整，等更多数据 */
        if(hlen < 0) { g_rxlen = 0; break; }

        size_t total = (size_t)hlen + body;
        if(total > g_rxlen) break;         /* 等完整包 */

        switch(type) {
        case MQTT_CONNACK: {
            /* [0]=连接标志 [1]=返回码，返回码 0 成功 */
            if(body >= 2 && g_rx[hlen + 1] == 0) {
                printf("[mqtt] 已连接 %s:%u\n", g_host, g_port);
                g_connected  = true;
                g_connecting = false;
                g_last_recv_ms = now_ms();
                /* 连接成功后自动补订阅 */
                if(g_sub_topic[0]) {
                    int n = build_subscribe(g_tx, g_sub_topic);
                    sock_send(g_tx, n);
                }
            }
            else {
                printf("[mqtt] CONNACK 拒绝（返回码 %u），重连\n",
                       body >= 2 ? g_rx[hlen + 1] : 0xFF);
                g_last_recv_ms = 0;
            }
            break;
        }
        case MQTT_PUBLISH: {
            handle_publish_frame(g_rx + hlen, body);
            /* QoS1 需回 PUBACK：此处简化只做 QoS0 订阅，不回 */
            break;
        }
        case MQTT_PUBACK:
        case MQTT_SUBACK:
        case MQTT_PINGRESP:
            g_last_recv_ms = now_ms();
            break;
        default:
            g_last_recv_ms = now_ms();
            break;
        }

        /* 移走已处理的包 */
        memmove(g_rx, g_rx + total, g_rxlen - total);
        g_rxlen -= total;
    }
}

static void mqtt_recv(void)
{
    if(g_sock < 0) return;
    uint8_t tmp[MQTT_RXBUF_MAX];
    for(;;) {
        int n = (int)recv(g_sock, tmp, sizeof(tmp), 0);
        if(n > 0) {
            if(g_rxlen + (size_t)n > sizeof(g_rx)) {
                g_rxlen = 0;
                continue;
            }
            memcpy(g_rx + g_rxlen, tmp, (size_t)n);
            g_rxlen += (size_t)n;
            if(g_rxlen >= sizeof(g_rx)) continue;
        }
        else if(n == 0) {
            printf("[mqtt] 连接被关闭\n");
            sock_close();
            return;
        }
        else if(errno != EAGAIN && errno != EWOULDBLOCK) {
            printf("[mqtt] recv 错误: %s\n", strerror(errno));
            sock_close();
            return;
        }
        break;   /* EAGAIN：本轮读完 */
    }
    mqtt_process_rx();
}

/* ============ 状态机驱动 ============ */
static void mqtt_machine(void)
{
    int64_t n = now_ms();

    /* 未初始化（未设置 GW_MQTT）时不连接 */
    if(!g_enabled) return;

    if(!g_connecting && !g_connected) {
        /* 断开状态：等待退避时间后重连 */
        if(n - g_last_connect_ms >= g_reconnect_ms) {
            printf("[mqtt] 连接 %s:%u ...\n", g_host, g_port);
            g_last_connect_ms = n;
            if(sock_connect() == 0) {
                /* 继续走 TCP_CONNECTING */
            }
        }
        return;
    }

    /* TCP/MQTT 连接建立中 */
    if(g_connecting && !g_connected) {
        struct pollfd pfd = { .fd = g_sock, .events = POLLOUT };
        int pr = poll(&pfd, 1, 0);
        if(pr > 0) {
            int soerr = 0;
            socklen_t slen = sizeof(soerr);
            getsockopt(g_sock, SOL_SOCKET, SO_ERROR, &soerr, &slen);
            if(soerr != 0) {
                printf("[mqtt] TCP 连接失败: %s\n", strerror(soerr));
                g_reconnect_ms = (g_reconnect_ms < RECONNECT_MAX_MS)
                                 ? g_reconnect_ms * 2 : RECONNECT_MAX_MS;
                sock_close();
                return;
            }
            /* TCP 已通，发 CONNECT */
            if(g_send_connect) {
                int n2 = build_connect(g_tx);
                if(sock_send(g_tx, n2) < 0) {
                    sock_close();
                    return;
                }
                g_send_connect = false;
            }
            mqtt_recv();
        }
        /* 握手超时保护：5s 无响应断开重连 */
        if(g_connecting && !g_connected && n - g_last_connect_ms > 5000) {
            g_reconnect_ms = (g_reconnect_ms < RECONNECT_MAX_MS)
                             ? g_reconnect_ms * 2 : RECONNECT_MAX_MS;
            sock_close();
        }
        return;
    }

    /* 已连接：周期心跳 + 收包 */
    if(g_connected) {
        mqtt_recv();
        if(!g_connected) return;    /* recv 中可能断线 */

        int64_t hb = (int64_t)g_keepalive * 1000 / 2;
        if(n - g_last_ping_ms >= hb) {
            g_tx[0] = (uint8_t)(MQTT_PINGREQ << 4);
            g_tx[1] = 0x00;
            if(sock_send(g_tx, 2) < 0) {
                goto fail;
            }
            g_last_ping_ms = n;
        }
        /* keepalive*1.5 无任何收包 -> 判定断线 */
        if(n - g_last_recv_ms > (int64_t)g_keepalive * 1500) {
            printf("[mqtt] 心跳超时，断开重连\n");
            goto fail;
        }
        return;
    }
    return;

fail:
    g_reconnect_ms = (g_reconnect_ms < RECONNECT_MAX_MS)
                     ? g_reconnect_ms * 2 : RECONNECT_MAX_MS;
    sock_close();
}

/* ============ 对外接口 ============ */
void mqtt_client_init(const char * host, uint16_t port,
                      const char * client_id, uint16_t keepalive,
                      mqtt_msg_cb_t cb, void * user)
{
    if(host) snprintf(g_host, sizeof(g_host), "%s", host);
    g_port     = port ? port : 1883;
    g_keepalive = keepalive ? keepalive : KEEPALIVE_DEF;
    if(client_id && client_id[0]) {
        snprintf(g_client_id, sizeof(g_client_id), "%s", client_id);
    }
    g_msg_cb   = cb;
    g_user     = user;
    g_reconnect_ms = RECONNECT_MIN_MS;
    g_last_connect_ms = 0;
    g_enabled  = true;
    printf("[mqtt] client 初始化: %s:%u id=%s keepalive=%us\n",
           g_host, g_port, g_client_id, g_keepalive);
}

void mqtt_client_poll(void)
{
    mqtt_machine();
}

int mqtt_client_publish(const char * topic, const void * payload, int len, uint8_t qos)
{
    if(!g_connected || !topic) return -1;
    if(len > MQTT_PAYLOAD_MAX) len = MQTT_PAYLOAD_MAX;
    int n = build_publish(g_tx, topic, payload, len, qos & 1);
    return (sock_send(g_tx, n) < 0) ? -1 : 0;
}

int mqtt_client_subscribe(const char * topic)
{
    if(!topic) return -1;
    snprintf(g_sub_topic, sizeof(g_sub_topic), "%s", topic);
    if(!g_connected) return 0;    /* 连接建立后自动补订 */
    int n = build_subscribe(g_tx, topic);
    return (sock_send(g_tx, n) < 0) ? -1 : 0;
}

bool mqtt_client_is_connected(void)
{
    return g_connected;
}

void mqtt_client_deinit(void)
{
    if(g_sock >= 0) {
        uint8_t d[2] = { (uint8_t)(MQTT_DISCONNECT << 4), 0x00 };
        sock_send(d, 2);
    }
    sock_close();
    g_msg_cb = NULL;
}
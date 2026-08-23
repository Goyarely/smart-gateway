/**
 * @file ui_bridge.c
 * @brief 面板 <-> 网关 协议客户端实现
 *
 * 状态机：
 *   未连接(每1s重试) -> 非阻塞connect -> poll检测完成 -> 已连接
 *   -> 发REGISTER -> 收到REGISTER_ACK -> 已注册(每2s心跳)
 *   -> 收帧（DATA_REPORT / SET_ACK）等
 */
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>

#include "lvgl/lvgl.h"       /* 用 lv_tick_get() 计时 */
#include "protocol.h"
#include "ui_bridge.h"
#include "ui_data.h"
#include "ui_chart.h"        /* 数据上报驱动图表实时刷新 */
#include "ui.h"              /* 更新状态区 Label（在线设备数） */
#include "ui_setting.h"      /* 设置页"在线设备数"联动 */

/* ============ 静态状态 ============ */
static int      g_sock       = -1;
static bool     g_connected  = false;
static bool     g_connecting = false;
static uint16_t g_seq        = 0;
static uint32_t g_last_hb    = 0;
static uint32_t g_last_reg   = 0;   /* 上次发 REGISTER 时间，限速防风暴 */
static uint32_t g_conn_try   = 0;
static bool     g_registered = false;

/* 网关地址 */
static char     g_host[64] = "127.0.0.1";
static int      g_port     = 8888;

/* 收帧缓冲（粘包拆帧） */
static uint8_t  g_rxbuf[FRAME_MAX_LEN * 2];
static size_t   g_rxlen = 0;

/* 注册重发间隔（ms），防止收不到 ACK 时的注册风暴 */
#define REGISTER_RETRY_MS  500

/* 已接收消息计数（显示在 Screen1 状态区） */
static uint32_t g_msg_count = 0;

/* 状态回调 */
static ui_bridge_status_cb_t g_status_cb = NULL;

static void notify_status(const char * text)
{
    if(g_status_cb) g_status_cb(text);
}

/* ============ 发送 ============ */
static int bridge_send(uint8_t type, const uint8_t * payload, size_t plen)
{
    if(g_sock < 0) return -1;
    uint8_t buf[FRAME_MAX_LEN];
    int n = frame_pack(buf, type, PANEL_DEV_ID, g_seq++, payload, plen);
    if(n < 0) return -1;
    int wr = (int)send(g_sock, buf, n, MSG_NOSIGNAL);
    if(wr <= 0) return -1;
    return 0;
}

/* ============ 连接 ============ */
static int bridge_connect(void)
{
    if(g_sock >= 0) { close(g_sock); g_sock = -1; }

    g_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(g_sock < 0) return -1;

    int flags = fcntl(g_sock, F_GETFL, 0);
    fcntl(g_sock, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family      = AF_INET;
    sa.sin_port        = htons((uint16_t)g_port);
    if(inet_pton(AF_INET, g_host, &sa.sin_addr) != 1) {
        close(g_sock);
        g_sock = -1;
        return -1;
    }

    int ret = connect(g_sock, (struct sockaddr *)&sa, sizeof(sa));
    if(ret != 0 && errno != EINPROGRESS) {
        LV_LOG_USER("[bridge] connect %s:%d failed: %s", g_host, g_port, strerror(errno));
        close(g_sock);
        g_sock = -1;
        return -1;
    }
    LV_LOG_USER("[bridge] connect %s:%d 发起 (ret=%d errno=%s)", g_host, g_port, ret,
                ret != 0 ? strerror(errno) : "ok");
    g_connecting = true;
    return 0;
}

/* ============ 收帧：粘包拆帧并逐帧处理 ============ */
static void bridge_recv(void)
{
    if(g_sock < 0) return;

    uint8_t tmp[FRAME_MAX_LEN];
    int n = (int)recv(g_sock, tmp, sizeof(tmp), 0);
    if(n > 0) {
        /* 缓冲溢出：清空重对齐，避免粘包永久错乱 */
        if(g_rxlen + (size_t)n > sizeof(g_rxbuf)) {
            LV_LOG_WARN("[bridge] 收帧缓冲溢出，重置");
            g_rxlen = 0;
            if((size_t)n > sizeof(g_rxbuf)) return;   /* 单包超限，丢弃 */
            memcpy(g_rxbuf, tmp, n);
            g_rxlen = (size_t)n;
        }
        else {
            memcpy(g_rxbuf + g_rxlen, tmp, n);
            g_rxlen += (size_t)n;
        }
    }
    else if(n == 0) {
        g_connected = false; g_registered = false; g_rxlen = 0;
        close(g_sock); g_sock = -1;
        notify_status("网关断开");
        return;
    }
    else if(errno != EAGAIN && errno != EWOULDBLOCK) {
        g_connected = false; g_registered = false; g_rxlen = 0;
        close(g_sock); g_sock = -1;
        notify_status("网关断开");
        return;
    }

    while(g_rxlen >= FRAME_HEAD_LEN + FRAME_CRC_LEN) {
        if(rd_be16(g_rxbuf) != FRAME_MAGIC) {
            memmove(g_rxbuf, g_rxbuf + 1, g_rxlen - 1);
            g_rxlen--;
            continue;
        }
        uint16_t len = rd_be16(g_rxbuf + 3);
        size_t total = (size_t)len + FRAME_CRC_LEN;
        if(total > g_rxlen) break;
        if(total > sizeof(g_rxbuf)) { g_rxlen = 0; break; }

        uint8_t  type; uint32_t dev; uint16_t seq;
        const uint8_t * payload; size_t plen;
        if(frame_parse(g_rxbuf, total, &type, &dev, &seq, &payload, &plen) == 0) {
            if(type == CMD_REGISTER_ACK) {
                g_registered = true;
                notify_status("已注册");
            }
            else if(type == CMD_DATA_REPORT) {
                /* 设备数据上报，转发自 server：payload = sensor(1) + value(1) */
                if(plen >= 2) {
                    uint8_t sensor = payload[0];
                    uint8_t value  = payload[1];
                    char val[16];
                    /* 更新数据页（sensor 1=温度 2=湿度 3=功耗）+ 驱动图表实时刷新 */
                    switch(sensor) {
                    case 1:
                        snprintf(val, sizeof(val), "%d", value);
                        ui_data_update_value(0, val);            /* 温度卡片 */
                        ui_data_add_history("实时", "设备", "温度", value, "°C");
                        ui_chart_add_temp((float)value);         /* Screen1 温度趋势折线图 */
                        ui_chart_set_mini(0, value);             /* Screen4 温度迷你柱状图 */
                        break;
                    case 2:
                        snprintf(val, sizeof(val), "%d", value);
                        ui_data_update_value(1, val);            /* 湿度卡片 */
                        ui_data_add_history("实时", "设备", "湿度", value, "%");
                        ui_chart_set_mini(1, value);             /* Screen4 湿度迷你柱状图 */
                        break;
                    case 3:
                        snprintf(val, sizeof(val), "%d", value);
                        ui_data_update_value(2, val);            /* 功耗卡片 */
                        ui_data_add_history("实时", "设备", "功耗", value, "W");
                        ui_chart_set_mini(2, value);             /* Screen4 功耗迷你柱状图 */
                        break;
                    default:
                        ui_data_add_history("实时", "设备", "数据", value, "");
                        break;
                    }
                }
            }
            else if(type == CMD_SET_ACK) {
                /* payload: result(1)，0 成功 */
            }
            else if(type == CMD_STAT) {
                /* 在线设备数通知：payload = 设备数(1) */
                if(plen >= 1) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "在线设备：%d 台", payload[0]);
                    if(ui_Label20) lv_label_set_text(ui_Label20, buf);
                    /* 同步设置页"在线设备数" */
                    ui_setting_update_devices(payload[0]);
                }
            }
        }

        /* 统计已接收消息数（Screen1 状态区） */
        g_msg_count++;
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "已收消息：%u 条", g_msg_count);
            if(ui_Label23) lv_label_set_text(ui_Label23, buf);
        }

        memmove(g_rxbuf, g_rxbuf + total, g_rxlen - total);
        g_rxlen -= total;
    }
}

/* ============ 周期处理 ============ */
void ui_bridge_poll(void)
{
    uint32_t now = lv_tick_get();

    if(!g_connected) {
        if(g_connecting) {
            struct pollfd pfd = { .fd = g_sock, .events = POLLOUT };
            int pr = poll(&pfd, 1, 0);
            if(pr > 0) {
                int soerr = 0;
                socklen_t slen = sizeof(soerr);
                getsockopt(g_sock, SOL_SOCKET, SO_ERROR, &soerr, &slen);
                if(soerr == 0) {
                    LV_LOG_USER("[bridge] 连接成功，发送 REGISTER");
                    g_connecting = false;
                    g_connected  = true;
                    g_rxlen      = 0;
                    bridge_send(CMD_REGISTER, NULL, 0);
                    notify_status("连接网关...");
                    /* Screen1 状态区：连接状态 + MQTT 状态（简化） */
                    if(ui_Label24) lv_label_set_text(ui_Label24, "连接状态：已连接");
                    if(ui_Label25) lv_label_set_text(ui_Label25, "MQTT：未接入");
                }
                else {
                    LV_LOG_USER("[bridge] 连接失败 soerr=%d (%s)", soerr, strerror(soerr));
                    close(g_sock); g_sock = -1;
                    g_connecting = false;
                    g_conn_try   = now;
                    notify_status("网关未连接");
                }
            }
        }
        else if(now - g_conn_try >= 1000) {
            bridge_connect();
            g_conn_try = now;
        }
        return;
    }

    /* 已连接：先收帧（处理 REGISTER_ACK 等），再决定发注册/心跳 */
    bridge_recv();

    if(!g_registered) {
        /* 注册未确认：限速重发（500ms），避免每 50ms 一次的风暴 */
        if(now - g_last_reg >= REGISTER_RETRY_MS) {
            bridge_send(CMD_REGISTER, NULL, 0);
            g_last_reg = now;
        }
    }
    else if(now - g_last_hb >= 2000) {
        bridge_send(CMD_HEARTBEAT, NULL, 0);
        g_last_hb = now;
    }
}

/* ============ 对外接口 ============ */
void ui_bridge_init(const char * host, int port, ui_bridge_status_cb_t status_cb)
{
    if(host) snprintf(g_host, sizeof(g_host), "%s", host);
    g_port = port;
    g_status_cb = status_cb;
    g_rxlen = 0;
    LV_LOG_USER("[bridge] init: %s:%d", g_host, g_port);
    ui_bridge_poll();   /* 首次尝试连接 */
}

int ui_bridge_send(uint8_t type, const uint8_t * payload, size_t plen)
{
    if(!g_connected || !g_registered) return -1;
    return bridge_send(type, payload, plen);
}

bool ui_bridge_is_connected(void)  { return g_connected; }
bool ui_bridge_is_registered(void) { return g_registered; }

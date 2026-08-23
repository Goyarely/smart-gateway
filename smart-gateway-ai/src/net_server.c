/**
 * @file net_server.c
 * @brief epoll 网关服务器实现
 *
 * 功能：
 *  - epoll 多路复用，同时接受面板 + 设备连接
 *  - 设备注册(REGISTER)/心跳(HEARTBEAT)/超时踢出(5s)
 *  - 命令路由：面板 SET_LIGHT/FAN/SCENE → 转发目标设备，回 SET_ACK
 *  - 设备 DATA_REPORT → 记录/可转发面板
 *
 * 命令路由表（面板 dev_id=0x00F10001，目标设备按此映射）：
 *    SET_LIGHT(0x04) -> 灯光设备 0x00F00001
 *    SET_FAN  (0x05) -> 风扇设备 0x00F00002
 *    SET_SCENE(0x06) -> 场景设备 0x00F00003
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "protocol.h"
#include "device.h"
#include "net.h"
#include "rule_engine.h"
#include "mqtt_client.h"
#include "json_util.h"

#define MAX_CLIENTS     128
#define MAX_EVENTS      128
#define PANEL_DEV_ID    0x00F10001

/* 路由表：面板命令 -> 目标设备 */
typedef struct {
    uint8_t  cmd;
    uint32_t target_dev;
    const char * name;
} route_t;
static const route_t g_routes[] = {
    { CMD_SET_LIGHT, 0x00F00001, "灯光" },
    { CMD_SET_CT,    0x00F00004, "色温" },
    { CMD_SET_FAN,   0x00F00002, "风扇" },
    { CMD_SET_SCENE, 0x00F00003, "场景" },
};

/* 每个连接一个收帧缓冲 */
typedef struct {
    int      fd;
    bool     in_use;
    uint8_t  rx[FRAME_MAX_LEN * 2];
    size_t   rxlen;
} client_t;

static client_t g_clients[MAX_CLIENTS];

static client_t * client_get(int fd)
{
    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(g_clients[i].in_use && g_clients[i].fd == fd) return &g_clients[i];
    }
    return NULL;
}

static client_t * client_alloc(int fd)
{
    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(!g_clients[i].in_use) {
            g_clients[i].in_use = true;
            g_clients[i].fd     = fd;
            g_clients[i].rxlen  = 0;
            return &g_clients[i];
        }
    }
    return NULL;
}

static void client_free(int fd)
{
    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(g_clients[i].in_use && g_clients[i].fd == fd) {
            g_clients[i].in_use = false;
            return;
        }
    }
}

/* 发送一帧到指定 fd */
static int send_frame(int fd, uint8_t type, uint32_t dev_id, uint16_t seq,
                      const uint8_t * payload, size_t plen)
{
    uint8_t buf[FRAME_MAX_LEN];
    int n = frame_pack(buf, type, dev_id, seq, payload, plen);
    if(n < 0) return -1;
    int wr = (int)send(fd, buf, n, MSG_NOSIGNAL);
    return (wr <= 0) ? -1 : 0;
}

/* 命令路由：面板 SET_* -> 目标设备
 * from_fd < 0 表示服务端主动下发（规则引擎/云端），不回 ACK */
static void route_command(int from_fd, uint8_t type, uint16_t seq,
                          const uint8_t * payload, size_t plen)
{
    /* 查路由表 */
    uint32_t target = 0;
    const char * name = "未知";
    for(size_t i = 0; i < sizeof(g_routes) / sizeof(g_routes[0]); i++) {
        if(g_routes[i].cmd == type) {
            target = g_routes[i].target_dev;
            name   = g_routes[i].name;
            break;
        }
    }
    if(target == 0) return;

    device_t * dev = device_find(target);
    printf("[net] 路由 %s 命令 -> 0x%08X%s, payload=", name, target,
           dev ? "(在线)" : "(离线)");
    for(size_t i = 0; i < plen; i++) printf("%02X ", payload[i]);
    printf("\n");

    /* 若目标设备在线，转发原帧；否则仅记录（回 ACK 告知面板） */
    if(dev) {
        /* 转发命令给设备（用目标设备自己的 fd） */
        send_frame(dev->fd, type, target, seq, payload, plen);
    }

    /* 回 SET_ACK 给面板（服务端主动下发不回） */
    if(from_fd >= 0) {
        uint8_t result = 0;
        send_frame(from_fd, CMD_SET_ACK, PANEL_DEV_ID, seq, &result, 1);
    }
}

/* ==================== 规则引擎 ====================
 * 本地自动化：温度联动风扇、功耗阈值告警。
 * 规则触发时由服务端主动向目标设备下发命令（不回 ACK）。
 */
static void rule_action_cb(const rule_t * rule, void * user)
{
    (void)user;
    if(rule->action_cmd == 0 || rule->action_plen == 0) {
        printf("[rule] %s：仅记录（无下发动作）\n", rule->name);
        return;
    }
    printf("[rule] %s：下发命令 cmd=0x%02X -> 0x%08X\n",
           rule->name, rule->action_cmd, rule->action_dev);
    route_command(-1, rule->action_cmd, 0,
                  rule->action_payload, rule->action_plen);
}

/* 找面板的 fd（dev_id == PANEL_DEV_ID），无则 -1 */
static int find_panel_fd(void)
{
    device_t * d = device_find(PANEL_DEV_ID);
    return d ? d->fd : -1;
}

/* ==================== 模拟传感器数据源 ====================
 * 演示用途：网关没有任何真实设备上报时，周期性地向面板广播
 * 模拟的温度/湿度/功耗数据，驱动面板图表实时刷新（对齐演示网页）。
 * 设置环境变量 GW_SIM=0 可关闭模拟。
 * sensor: 1=温度 2=湿度 3=功耗
 */
#define SIM_SENSOR_DEV   0x00F00005   /* 模拟传感器设备 */
#define SIM_INTERVAL_MS  1000
static uint64_t g_sim_last = 0;
static int g_sim_temp = 26, g_sim_humi = 58, g_sim_power = 18;

/* ==================== MQTT 云接入 ====================
 * 环境变量 GW_MQTT="host:port" 启用（如 GW_MQTT=192.168.1.10:1883）。
 * 数据流：
 *  - 上报：模拟/真实传感器数据 JSON 发到 topic "gw/sensor"
 *  - 下行：订阅 "gw/cmd"，收到云端 JSON 命令转成内部路由下发
 * 云端命令格式：
 *    {"cmd":"set_fan","value":1800}   -> 风扇 1800rpm
 *    {"cmd":"set_light","value":80}   -> 灯光亮度 80%
 *    {"cmd":"set_ct","value":4000}    -> 色温 4000K
 *    {"cmd":"set_scene","value":1}    -> 场景 1（明亮）
 */
static void mqtt_report_sensor(uint8_t sensor, int value)
{
    if(!mqtt_client_is_connected()) return;
    char buf[96];
    json_build_t jb;
    json_build_begin(&jb, buf, sizeof(buf));
    json_build_add_int(&jb, "sensor", sensor);
    json_build_add_int(&jb, "value", value);
    json_build_end(&jb);
    mqtt_client_publish("gw/sensor", buf, (int)strlen(buf), 0);
}

/* 云端下行命令回调（MQTT 线程/主循环里调用） */
static void on_mqtt_msg(const char * topic, const uint8_t * payload, int len, void * user)
{
    (void)user;
    if(strcmp(topic, "gw/cmd") != 0) return;

    char js[128];
    if(len >= (int)sizeof(js)) len = (int)sizeof(js) - 1;
    memcpy(js, payload, (size_t)len);
    js[len] = '\0';
    printf("[mqtt] 云端命令: %s\n", js);

    int value = 0;
    if(json_get_int(js, "value", &value) != 0) value = 0;

    uint8_t p[8];
    size_t  n = 0;
    uint8_t cmd = 0;
    char cname[32];
    if(json_get_str(js, "cmd", cname, sizeof(cname)) == 0) {
        if(strcmp(cname, "set_fan") == 0) {
            cmd = CMD_SET_FAN;
            p[0] = (uint8_t)(value >> 8); p[1] = (uint8_t)(value & 0xFF); n = 2;
        }
        else if(strcmp(cname, "set_light") == 0) {
            cmd = CMD_SET_LIGHT;
            p[0] = (uint8_t)(value & 0xFF); n = 1;
        }
        else if(strcmp(cname, "set_ct") == 0) {
            cmd = CMD_SET_CT;
            p[0] = (uint8_t)(value >> 8); p[1] = (uint8_t)(value & 0xFF); n = 2;
        }
        else if(strcmp(cname, "set_scene") == 0) {
            cmd = CMD_SET_SCENE;
            p[0] = (uint8_t)(value & 0xFF); n = 1;
        }
    }
    if(cmd) {
        route_command(-1, cmd, 0, p, n);
    }
}

static void sim_report(int panel_fd)
{
    if(panel_fd < 0) return;
    /* 温度 24~30 缓慢波动 */
    g_sim_temp += (rand() % 3) - 1;
    if(g_sim_temp < 24) g_sim_temp = 24;
    if(g_sim_temp > 30) g_sim_temp = 30;
    /* 湿度 50~70 */
    g_sim_humi += (rand() % 3) - 1;
    if(g_sim_humi < 50) g_sim_humi = 50;
    if(g_sim_humi > 70) g_sim_humi = 70;
    /* 功耗 10~30W */
    g_sim_power += (rand() % 5) - 2;
    if(g_sim_power < 10) g_sim_power = 10;
    if(g_sim_power > 30) g_sim_power = 30;

    uint8_t p[2];
    p[0] = 1; p[1] = (uint8_t)g_sim_temp;  send_frame(panel_fd, CMD_DATA_REPORT, SIM_SENSOR_DEV, 0, p, 2);
    mqtt_report_sensor(1, g_sim_temp);
    p[0] = 2; p[1] = (uint8_t)g_sim_humi;  send_frame(panel_fd, CMD_DATA_REPORT, SIM_SENSOR_DEV, 0, p, 2);
    mqtt_report_sensor(2, g_sim_humi);
    p[0] = 3; p[1] = (uint8_t)g_sim_power; send_frame(panel_fd, CMD_DATA_REPORT, SIM_SENSOR_DEV, 0, p, 2);
    mqtt_report_sensor(3, g_sim_power);
}

/* 面板是否已注册在线 */
static int is_panel_registered(void)
{
    return find_panel_fd() >= 0;
}

/* 通知面板：在线设备数变化 */
static void notify_panel_stat(int panel_fd)
{
    if(panel_fd < 0) return;
    uint8_t cnt = (uint8_t)(device_count() - 1);   /* 减去面板自己 */
    send_frame(panel_fd, CMD_STAT, PANEL_DEV_ID, 0, &cnt, 1);
}

/* 处理一个完整帧 */
static void handle_frame(int fd, const uint8_t * data, size_t len)
{
    uint8_t  type; uint32_t dev_id; uint16_t seq;
    const uint8_t * payload; size_t plen;
    if(frame_parse(data, len, &type, &dev_id, &seq, &payload, &plen) != 0) {
        return;
    }
    uint64_t now = device_now_ms();

    switch(type) {
    case CMD_REGISTER: {
        /* 设备/面板注册：面板 dev_id=0x00F10001，真实设备各自 dev_id */
        device_register(dev_id, fd, now);
        uint8_t result = 0;
        send_frame(fd, CMD_REGISTER_ACK, dev_id, seq, &result, 1);
        printf("[net] 注册 0x%08X fd=%d, 当前在线设备 %d\n",
               dev_id, fd, device_count());
        /* 设备数变化，通知面板刷新状态区（面板自己注册时不用） */
        if(dev_id != PANEL_DEV_ID) {
            notify_panel_stat(find_panel_fd());
        }
        break;
    }
    case CMD_HEARTBEAT: {
        device_touch(dev_id, now);
        break;
    }
    case CMD_SET_LIGHT:
    case CMD_SET_CT:
    case CMD_SET_FAN:
    case CMD_SET_SCENE:
        route_command(fd, type, seq, payload, plen);
        break;
    case CMD_DATA_REPORT: {
        printf("[net] DATA_REPORT dev=0x%08X sensor=%u value=%u\n",
               dev_id,
               plen >= 1 ? payload[0] : 0,
               plen >= 2 ? payload[1] : 0);
        /* 喂本地规则引擎（sensor=payload[0], value=payload[1]） */
        if(plen >= 2) {
            rule_engine_feed(payload[0], payload[1], (uint32_t)device_now_ms());
        }
        /* 转发给面板（面板不转给自己） */
        int pf = find_panel_fd();
        if(pf >= 0 && dev_id != PANEL_DEV_ID) {
            send_frame(pf, CMD_DATA_REPORT, dev_id, seq, payload, plen);
        }
        break;
    }
    default:
        printf("[net] 未知命令 type=%u dev=0x%08X\n", type, dev_id);
        break;
    }
}

/* 客户端收数据：粘包拆帧 */
static void handle_recv(client_t * c)
{
    uint8_t tmp[FRAME_MAX_LEN];
    int n = (int)recv(c->fd, tmp, sizeof(tmp), 0);
    if(n <= 0) {
        if(n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
            /* 客户端断开 */
            device_remove_fd(c->fd);
            printf("[net] 客户端断开 fd=%d\n", c->fd);
            close(c->fd);
            client_free(c->fd);
        }
        return;
    }
    /* 缓冲溢出：说明粘包解析失效，清空整个缓冲重新对齐，避免永久错乱 */
    if(c->rxlen + (size_t)n > sizeof(c->rx)) {
        printf("[net] fd=%d 收帧缓冲溢出，重置缓冲\n", c->fd);
        c->rxlen = 0;
        if((size_t)n > sizeof(c->rx)) return;   /* 单包超限，丢弃 */
        memcpy(c->rx, tmp, n);
        c->rxlen = (size_t)n;
    }
    else {
        memcpy(c->rx + c->rxlen, tmp, n);
        c->rxlen += (size_t)n;
    }

    /* 拆帧 */
    while(c->rxlen >= FRAME_HEAD_LEN + FRAME_CRC_LEN) {
        if(rd_be16(c->rx) != FRAME_MAGIC) {
            memmove(c->rx, c->rx + 1, c->rxlen - 1);
            c->rxlen--;
            continue;
        }
        uint16_t len = rd_be16(c->rx + 3);
        size_t total = (size_t)len + FRAME_CRC_LEN;
        if(total > c->rxlen) break;
        if(total > sizeof(c->rx)) { c->rxlen = 0; break; }
        handle_frame(c->fd, c->rx, total);
        memmove(c->rx, c->rx + total, c->rxlen - total);
        c->rxlen -= total;
    }
}

int net_server_start(uint16_t port)
{
    setbuf(stdout, NULL);   /* 日志实时输出（重定向到文件也可见） */
    device_init();

    /* 本地规则引擎：温度联动风扇 + 功耗阈值告警 */
    {
        size_t nr = 0;
        const rule_t * rules = rule_engine_defaults(&nr);
        rule_engine_configure(rules, nr, rule_action_cb, NULL);
    }

    /* MQTT 云接入（GW_MQTT="host:port"，未设置则跳过） */
    {
        const char * mqtt = getenv("GW_MQTT");
        if(mqtt && *mqtt) {
            char host[64] = "";
            int  port_ = 1883;
            if(sscanf(mqtt, "%63[^:]:%d", host, &port_) >= 1) {
                mqtt_client_init(host, (uint16_t)port_, "smart-gateway-ai", 30,
                                 on_mqtt_msg, NULL);
                mqtt_client_subscribe("gw/cmd");
            }
        }
    }

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if(lfd < 0) { perror("socket"); return -1; }
    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);
    if(bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(lfd); return -1;
    }
    if(listen(lfd, 64) < 0) { perror("listen"); close(lfd); return -1; }

    int efd = epoll_create1(0);
    if(efd < 0) { perror("epoll_create1"); close(lfd); return -1; }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = lfd;
    epoll_ctl(efd, EPOLL_CTL_ADD, lfd, &ev);

    printf("[net] 网关服务器监听 :%u\n", port);

    struct epoll_event events[MAX_EVENTS];
    while(1) {
        int n = epoll_wait(efd, events, MAX_EVENTS, 1000);

        for(int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            if(fd == lfd) {
                /* 新连接 */
                struct sockaddr_in caddr;
                socklen_t clen = sizeof(caddr);
                int cfd = accept(lfd, (struct sockaddr *)&caddr, &clen);
                if(cfd < 0) continue;
                int flags = fcntl(cfd, F_GETFL, 0);
                fcntl(cfd, F_SETFL, flags | O_NONBLOCK);
                if(client_alloc(cfd) == NULL) { close(cfd); continue; }
                ev.events = EPOLLIN;
                ev.data.fd = cfd;
                epoll_ctl(efd, EPOLL_CTL_ADD, cfd, &ev);
                char ip[32] = "";
                inet_ntop(AF_INET, &caddr.sin_addr, ip, sizeof(ip));
                printf("[net] 新连接 fd=%d from %s\n", cfd, ip);
            }
            else {
                client_t * c = client_get(fd);
                if(c) handle_recv(c);
            }
        }

        /* 空闲或周期：清理超时设备 */
        if(n == 0) {
            int dead[MAX_CLIENTS];
            int cnt = device_remove_if_timeout(device_now_ms(), dead, MAX_CLIENTS);
            for(int i = 0; i < cnt; i++) {
                client_t * c = client_get(dead[i]);
                if(c) {
                    close(c->fd);
                    epoll_ctl(efd, EPOLL_CTL_DEL, c->fd, NULL);
                    client_free(c->fd);
                }
            }
            /* 有设备被踢出，通知面板刷新在线设备数 */
            if(cnt > 0) {
                notify_panel_stat(find_panel_fd());
            }
        }

        /* 演示用：面板已注册且未通过环境变量关闭时，周期广播模拟传感器数据 */
        if(getenv("GW_SIM") && !strcmp(getenv("GW_SIM"), "0")) {
            /* 关闭模拟 */
        }
        else if(is_panel_registered() && device_now_ms() - g_sim_last >= SIM_INTERVAL_MS) {
            g_sim_last = device_now_ms();
            sim_report(find_panel_fd());
        }

        /* MQTT 云接入：周期驱动连接/收发/心跳/重连 */
        mqtt_client_poll();
    }
    return 0;
}

void net_server_print_info(void)
{
    printf("smart-gateway 网关\n");
    printf("命令路由：SET_LIGHT->0x00F00001(灯光), SET_CT->0x00F00004(色温), SET_FAN->0x00F00002(风扇), SET_SCENE->0x00F00003(场景)\n");
    printf("面板 dev_id=0x00F10001, 心跳超时 %dms\n", DEVICE_TIMEOUT_MS);
}

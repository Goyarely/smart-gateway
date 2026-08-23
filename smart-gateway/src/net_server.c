#include <arpa/inet.h>
#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <asm-generic/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "net.h"
#include "protocol.h"
#include "ringbuf.h"

#define MAX_EVENTS 64
#define RECV_BUF 4096

/* 每个连接一个上下文：fd + 独立环形缓冲区（字节流连接隔离）*/
typedef struct {
  int fd;
  ringbuf_t rb;
} conn_t;

static void set_nonblock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/*处理一帧：打印 + 回ACK*/
static void handle_frame(conn_t *conn, const frame_t *f) {
  printf("协议层 type=0x%02X dev=%u seq=%u len=%u\n", f->type, f->dev_id,
         f->seq, f->playload_len);
  uint8_t ack[FRAME_HEAD_LEN + MAX_PAYLOAD];
  int ack_len =
      frame_encode(ack, sizeof(ack), CMD_SET_ACK, f->dev_id, f->seq, NULL, 0);

  if (ack_len > 0) {
    send(conn->fd, ack, ack_len, 0);
  }
}

static void close_conn(conn_t *conn, int epfd) {
  int fd = conn->fd;
  epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
  close(fd);
  free(conn);
  printf("网络层设备已断开！\n");
}

/* 可读事件：循环recv（ET一次读完） -》进环形缓冲区-》循环拆帧处理*/
static void on_readable(conn_t *conn, int epfd) {
  uint8_t buf[RECV_BUF];
  while (1) {
    int r = recv(conn->fd, buf, sizeof(buf), 0);
    if (r > 0) {
      ringbuf_write(&conn->rb, buf, (size_t)r);
      frame_t frame;
      int ret;
      while ((ret = ringbuf_try_parse(&conn->rb, &frame)) != 0) {
        if (ret > 0) {
          handle_frame(conn, &frame);
        } else {
          printf("协议层坏帧，跳过1字节重新同步\n");
        }
      }
    } else if (r == 0) {
      close_conn(conn, epfd); // 对端关闭
    } else {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        break;                // 读干净了
      close_conn(conn, epfd); // 真正错误
      break;
    }
  }
}

int net_server_start(int port) {
  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    perror("socket");
    return -1;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);

  int no = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &no, sizeof(no));

  if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    return -1;
  }
  if (listen(listen_fd, 5) < 0) {
    perror("listen");
    return -1;
  }
  printf("[网络层] 监听 %d 端口...\n", port);

  set_nonblock(listen_fd);

  int epfd = epoll_create(1);
  if (epfd < 0) {
    perror("epoll_create");
    return -1;
  }

  struct epoll_event ev;
  ev.events = EPOLLIN | EPOLLET;
  ev.data.fd = listen_fd;
  epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);

  struct epoll_event events[MAX_EVENTS];
  while (1) {
    int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
    for (int i = 0; i < n; i++) {
      if (events[i].data.fd == listen_fd) {
        /* 新连接：循环 accept 到 EAGAIN */
        while (1) {
          int conn_fd = accept(listen_fd, NULL, NULL);
          if (conn_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
              break;
            perror("accept");
            break;
          }
          set_nonblock(conn_fd);

          conn_t *conn = calloc(1, sizeof(conn_t));
          conn->fd = conn_fd;
          ring_init(&conn->rb);

          ev.events = EPOLLIN | EPOLLET;
          ev.data.ptr = conn; /* ★ 把连接上下文挂在事件上 */
          epoll_ctl(epfd, EPOLL_CTL_ADD, conn_fd, &ev);
          printf("[网络层] 客户端连入 fd=%d\n", conn_fd);
        }
      } else {
        conn_t *conn = (conn_t *)events[i].data.ptr;
        on_readable(conn, epfd);
      }
    }
  }

  close(listen_fd);
  return 0;
}

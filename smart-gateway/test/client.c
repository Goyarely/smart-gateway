#include "protocol.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  if (argc < 3) {
    printf("用法：%s <ip> <port>\n", argv[0]);
  }

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(atoi(argv[2]));
  addr.sin_addr.s_addr = inet_addr(argv[1]);

  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(sock);
    return -1;
  }
  printf("客户端已连接\n");

  // 组一帧：设置灯光亮度 80%（dev_id = 2, seq =0x3F, payload=80）
  uint8_t payload = 80;
  uint8_t buf[FRAME_HEAD_LEN + MAX_PAYLOAD];
  int len =
      frame_encode(buf, sizeof(buf), CMD_SET_LIGHT, 2, 0x003F, &payload, 1);
  printf("客户端已发送 SET_LIGHT dev=2 亮度=80\n");

  // 等ACK
  uint8_t recv_buf[FRAME_HEAD_LEN + MAX_PAYLOAD];
  int r = recv(sock, recv_buf, sizeof(recv_buf), 0);
  if (r > 0) {
    frame_t frame;
    if (frame_decode(recv_buf, (size_t)r, &frame) > 0) {
      printf("客户端收到ACK type=0x%02X dev=%u seq=%u\n", frame.type,
             frame.dev_id, frame.seq);
    } else {
      printf("客户端收到数据但解析失败\n");
    }
  }
  close(sock);
  return 0;
}

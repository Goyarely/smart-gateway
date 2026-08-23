#include "net.h"
#include <stdio.h>

int main(void) {

  printf("[应用层] 网关启动\n");
  return net_server_start(6666);
}

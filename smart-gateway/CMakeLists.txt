cmake_minimum_required(VERSION 3.16)
project(smart_gateway C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(include)

add_executable(server
    src/main.c src/net_server.c
    src/net_server.c
    src/protocol.c
    src/ringbuf.c
)

add_executable(client
    test/client.c
    src/protocol.c
)

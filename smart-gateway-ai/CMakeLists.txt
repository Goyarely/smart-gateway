cmake_minimum_required(VERSION 3.11)
project(smart-gateway-ai)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# ============================================================
# LVGL Kconfig 配置（与 lv_port_linux 保持一致）
# .config 已开启：FBDEV + EVDEV（板子落地）
# PC 调试想用 SDL 窗口：把 .config 里 CONFIG_LV_USE_SDL 打开即可
# ============================================================
set(LV_BUILD_USE_KCONFIG ON CACHE BOOL "use kconfig" FORCE)
# 配置文件默认 .config（PC 用 SDL）；ARM 交叉编译时用 -DLVGL_CONFIG_FILE=../.config.arm 指定（关 SDL 用 FBDEV）
set(CONFIG_FILE "${LVGL_CONFIG_FILE}" CACHE FILEPATH "LVGL Kconfig 配置文件")
if(NOT CONFIG_FILE)
    set(CONFIG_FILE "${CMAKE_CURRENT_LIST_DIR}/.config")
endif()
set(DOTCONFIG "${CONFIG_FILE}")
set(DEFCONFIG "${CMAKE_CURRENT_LIST_DIR}/configs/get_started.defconfig")
set(LV_BUILD_KCONFIG_ROOT "${CMAKE_CURRENT_LIST_DIR}/Kconfig" CACHE PATH "path to root kconfig" FORCE)

if(EXISTS "${DOTCONFIG}")
    message(STATUS "Using .config found to build LVGL")
    set(LV_BUILD_DOTCONFIG_PATH "${DOTCONFIG}" CACHE PATH "path to .config" FORCE)
    unset(LV_BUILD_DEFCONFIG_PATH)
else()
    message(STATUS "No .config found, using ${DEFCONFIG}")
    set(LV_BUILD_DEFCONFIG_PATH "${DEFCONFIG}" CACHE PATH "path to defconfig" FORCE)
    unset(LV_BUILD_DOTCONFIG_PATH)
endif()

# 我们只做面板 UI，不构建 demos / examples，加快编译
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/lvgl)

# ============================================================
# 驱动后端（从 lv_port_linux/src/lib 拷贝而来）
# 按 .config 中的 CONFIG_* 宏决定编译哪些后端
# ============================================================
file(GLOB LV_DRIVER_SRC third_party/lv_drivers/*.c)
set(LV_DRIVER_INC third_party/lv_drivers)
set(LV_DRIVER_BACKEND_SRC)

function(lv_add_backend_if option dir file)
    if(${option})
        list(APPEND LV_DRIVER_BACKEND_SRC ${dir}/${file})
        set(LV_DRIVER_BACKEND_SRC ${LV_DRIVER_BACKEND_SRC} PARENT_SCOPE)
    endif()
endfunction()

lv_add_backend_if(CONFIG_LV_USE_LINUX_DRM   third_party/lv_drivers/display_backends drm.c)
lv_add_backend_if(CONFIG_LV_USE_LINUX_FBDEV third_party/lv_drivers/display_backends fbdev.c)
lv_add_backend_if(CONFIG_LV_USE_SDL         third_party/lv_drivers/display_backends sdl.c)
lv_add_backend_if(CONFIG_LV_USE_EVDEV       third_party/lv_drivers/indev_backends   evdev.c)

add_library(lvgl_linux ${LV_DRIVER_SRC} ${LV_DRIVER_BACKEND_SRC})
target_link_libraries(lvgl_linux PUBLIC lvgl::lvgl)
target_include_directories(lvgl_linux PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lv_drivers
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl/include)

# ============================================================
# lvglsim —— LVGL 控制面板（独立进程，连 server:8888）
# 入口 src/main.c，ui/ 为 SquareLine 导出代码（ui.c/ui_helpers.c/screens/...）
# ============================================================
file(GLOB UI_SRC
    src/main.c
    src/ui_bridge.c
    src/protocol.c
    src/json_util.c
    src/ui_data.c
    src/ui_light.c
    src/ui_fan.c
    src/ui_setting.c
    src/ui_chart.c
    src/ui_state.c
    ui/*.c
    ui/screens/*.c
    ui/components/*.c
    ui/fonts/*.c
    ui/images/*.c)

add_executable(lvglsim ${UI_SRC})
target_include_directories(lvglsim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lvglsim PRIVATE lvgl_linux pthread m dl)

# 若启用 SDL 后端则链接 SDL2
if(CONFIG_LV_USE_SDL)
    find_package(SDL2 QUIET)
    if(SDL2_FOUND)
        target_link_libraries(lvglsim PRIVATE SDL2::SDL2)
    endif()
endif()

# ============================================================
# 测试目标：自研 JSON 工具
# ============================================================
add_executable(test_json_util
    test/test_json_util.c
    src/json_util.c)
target_include_directories(test_json_util PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 协议编解码测试
add_executable(test_protocol
    test/test_protocol.c
    src/protocol.c)
target_include_directories(test_protocol PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 最小模拟网关（验证 lvglsim 协议交互）
add_executable(mock_server
    test/mock_server.c
    src/protocol.c)
target_include_directories(mock_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ============================================================
# 真正的网关 server（运行在 PC，进程 A）
# 设备管理 + 命令路由 + 心跳超时踢出 + 本地规则引擎 + MQTT 云接入
# ============================================================
add_executable(server
    src/server_main.c
    src/net_server.c
    src/device.c
    src/protocol.c
    src/json_util.c
    src/rule_engine.c
    src/mqtt_client.c)
target_include_directories(server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 规则引擎单元测试
add_executable(test_rule_engine
    test/test_rule_engine.c
    src/rule_engine.c
    src/protocol.c)
target_include_directories(test_rule_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

# MQTT 客户端单元测试（报文编解码 + 状态机）
add_executable(test_mqtt_client
    test/test_mqtt_client.c
    src/mqtt_client.c)
target_include_directories(test_mqtt_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

#!/bin/sh
#
# deploy.sh — smart-gateway-ai 一键编译/部署脚本
#
# 用法：
#   ./deploy.sh sdl          编译并运行 SDL 版面板（PC 模拟窗口）
#   ./deploy.sh sdl-nogw     只跑 SDL 面板，不自动启动 server
#   ./deploy.sh server       编译并（重启）PC 网关 server（含模拟数据源）
#   ./deploy.sh board        编译 ARM 版 + 上传开发板 + 杀旧进程 + 重启
#   ./deploy.sh arm-only     只编译 ARM 版，不上传（改普通代码快速检查编译）
#   ./deploy.sh help         显示帮助
#
# 前置：
#   - 交叉编译需 aarch64-linux-gnu-gcc
#   - 上传需 adb（开发板在线）
#   - 开发板 WiFi 断开时先执行：adb shell "sh /usr/bin/wifi_up.sh"
#
# 修改 .config / .config.arm 后，需手动重新 cmake 配置（脚本自动检测可选）：
#   cmake -DLVGL_CONFIG_FILE=$PWD/.config.arm -S . -B build-arm

set -u

PROJ="$(cd "$(dirname "$0")" && pwd)"
cd "$PROJ" || { echo "无法进入项目目录: $PROJ"; exit 1; }

# 开发板网关地址（PC wlo1 的 IP）
GW_IP="${GW_IP:-10.122.117.172}"
GW_PORT="${GW_PORT:-8888}"

# 开发板目标文件
BOARD_BIN="/data/lvglsim_new"
BOARD_LOG="/data/lvglsim.log"

# 编译线程数
JOBS="$(nproc 2>/dev/null || echo 4)"

echo_g() { printf "\033[32m[deploy]\033[0m %s\n" "$*"; }
echo_r() { printf "\033[31m[deploy]\033[0m %s\n" "$*" >&2; }

# ---------- 编译 SDL 版 ----------
build_sdl() {
    echo_g "编译 SDL 版 lvglsim..."
    cmake --build build --target lvglsim -j"$JOBS" || return 1
    echo_g "SDL 版编译完成: build/bin/lvglsim"
    return 0
}

# ---------- 编译并运行 PC server ----------
run_server() {
    echo_g "编译 server..."
    cmake --build build --target server -j"$JOBS" || return 1

    echo_g "停止旧 server..."
    pkill -f "$PROJ/build/bin/server" 2>/dev/null
    pkill -f "bin/server" 2>/dev/null
    sleep 1

    echo_g "启动 server (监听 :$GW_PORT)..."
    setsid ./build/bin/server "$GW_PORT" > /tmp/gw_server.log 2>&1 < /dev/null &
    sleep 2
    if pgrep -f "bin/server" > /dev/null; then
        echo_g "server 已启动，日志: /tmp/gw_server.log"
        tail -3 /tmp/gw_server.log
    else
        echo_r "server 启动失败，日志:"
        tail -5 /tmp/gw_server.log
        return 1
    fi
    return 0
}

# ---------- 编译 ARM 版 ----------
build_arm() {
    echo_g "交叉编译 ARM 版 lvglsim..."
    cmake --build build-arm --target lvglsim -j"$JOBS" || return 1
    echo_g "ARM 版编译完成: build-arm/bin/lvglsim"
    return 0
}

# ---------- 上传 + 重启开发板 ----------
deploy_board() {
    [ -f build-arm/bin/lvglsim ] || { echo_r "build-arm/bin/lvglsim 不存在，先编译"; return 1; }
    command -v adb >/dev/null 2>&1 || { echo_r "未找到 adb"; return 1; }

    # 1. 检查开发板在线
    if ! adb devices | grep -q "device$"; then
        echo_r "开发板未连上 adb（USB）"
        return 1
    fi

    # 2. 上传
    echo_g "上传到开发板 $BOARD_BIN ..."
    adb push build-arm/bin/lvglsim "$BOARD_BIN" || return 1

    # 3. 停旧进程（用 pkill -9 -x 精确匹配进程名 lvglsim_new，避免 -f 匹配到自身命令行而自杀）
    echo_g "停止旧 lvglsim 进程..."
    adb shell "pkill -9 -x lvglsim_new 2>/dev/null; sleep 2"

    # 4. 清日志 + 启动（nohup + & 后必须 sleep 保持 adb shell 会话，
    #    让进程 fork 并脱离会话（PPID 变 1），否则 adb shell 退出会清掉子进程）
    echo_g "启动开发板面板 (FBDEV, 连 $GW_IP:$GW_PORT)..."
    adb shell "rm -f $BOARD_LOG; nohup $BOARD_BIN -b FBDEV -h $GW_IP > $BOARD_LOG 2>&1 < /dev/null & sleep 3; echo started"

    # 5. 确认
    sleep 3
    echo "--- 开发板进程 ---"
    adb shell "ps -ef | grep -v grep | grep lvglsim"
    echo "--- 开发板日志（末尾）---"
    adb shell "tail -5 $BOARD_LOG"
    return 0
}

# ---------- 运行 SDL 面板 ----------
run_sdl() {
    build_sdl || return 1
    local start_gw="$1"   # "yes"=启动 server
    if [ "$start_gw" = "yes" ]; then
        # 若 server 没在跑则启动
        if ! pgrep -f "bin/server" > /dev/null; then
            run_server || return 1
        else
            echo_g "server 已在运行，跳过启动"
        fi
    fi
    echo_g "启动 SDL 面板 (Ctrl+C 退出)..."
    exec ./build/bin/lvglsim
}

# ---------- 帮助 ----------
usage() {
    cat <<'EOF'
smart-gateway-ai 部署脚本

用法: ./deploy.sh <命令>

命令:
  sdl           编译并运行 SDL 版面板（自动启动 server）
  sdl-nogw      只编译并运行 SDL 面板（不启动 server）
  server        编译并重启 PC 网关 server（含模拟数据源）
  board         编译 ARM 版 + 上传 + 杀旧进程 + 重启开发板
  arm-only      只编译 ARM 版（不上传）
  help          显示本帮助

环境变量:
  GW_IP         网关 IP（默认 10.122.117.172）
  GW_PORT       网关端口（默认 8888）

示例:
  ./deploy.sh board
  GW_IP=192.168.1.5 ./deploy.sh board
EOF
}

# ---------- 主入口 ----------
case "${1:-help}" in
    sdl)       run_sdl yes ;;
    sdl-nogw)  run_sdl no ;;
    server)    run_server ;;
    board)     build_arm && deploy_board ;;
    arm-only)  build_arm ;;
    help|-h|*) usage ;;
esac

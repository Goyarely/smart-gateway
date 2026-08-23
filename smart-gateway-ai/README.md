# smart-gateway-ai

多模态边缘智能网关 + LVGL 触摸屏控制面板（1024×600）。

- 网关：原纯 C、epoll 高并发网关（`server`）
- 面板：LVGL 独立进程 `lvglsim`，通过 TCP:8888 连网关，作为一台"面板设备"接入

详细设计见 [文档/架构设计.md](文档/架构设计.md)。

---

## 目录结构

```
smart-gateway-ai/
├── CMakeLists.txt              # 顶层构建：third_party/lvgl + lvgl_linux(驱动后端) + lvglsim
├── README.md
├── deploy.sh                   # ★ 一键编译/部署脚本（SDL 或开发板）
├── .config                     # LVGL Kconfig 配置（已开 SDL / FBDEV / EVDEV）
├── .config.arm                 # 交叉编译配置（FBDEV + 双缓冲，关 SDL）
├── Kconfig                     # 顶层 Kconfig（rsource third_party/lvgl/Kconfig）
├── arm.cmake                   # RK3568 交叉编译工具链
├── 文档/架构设计.md
│
├── include/                    # 网关 + 面板共用头文件（当前为空）
├── src/                        # ★ 面板业务入口 main.c（自写，LVGL 面板）
├── test/                       # 测试（当前为空）
│
├── ui/                         # ★ SquareLine 导出 UI（拷贝自 lv_port_linux/ui）
│   ├── ui.c  ui.h  ui_events.h  ui_helpers.c/h
│   ├── screens/                # ui_Screen1.c/h
│   ├── components/             # ui_comp_hook.c
│   ├── fonts/                  # ui_font_Font1.c
│   └── assets/                 # SVG / 图片素材
│
└── third_party/
    ├── lvgl/                   # LVGL 核心（9.x，拷贝自 lv_port_linux）
    └── lv_drivers/             # 驱动后端（拷贝自 lv_port_linux/src/lib）
        ├── driver_backends.c/h # 后端抽象 + 选择
        ├── backends.h
        ├── simulator_settings.h / simulator_util.c/h
        ├── mouse_cursor_icon.c
        ├── display_backends/   # sdl.c / fbdev.c / drm.c
        └── indev_backends/     # evdev.c
```

## 移植来源（成功范本）

面板移植完全参照 `/home/dcz/codes/10-lvgl/lv_port_linux`（已跑通 SquareLine 导出 UI）：

| 组件 | 来源 |
|------|------|
| `third_party/lvgl` | lv_port_linux 的 `lvgl/`（LVGL 9.x 核心，Kconfig 构建） |
| `third_party/lv_drivers` | lv_port_linux 的 `src/lib/`（删除 glfw3/wayland/x11） |
| `.config` / `Kconfig` | lv_port_linux 顶层（Kconfig 的 rsource 路径改为 `third_party/lvgl/Kconfig`） |
| `src/main.c` 入口框架 | lv_port_linux 的 `src/main.c`（流程：register→lv_init→group→init_backend→ui_init→run_loop） |
| `ui/` SquareLine 导出 | lv_port_linux 的 `ui/`（ui.c/ui_helpers.c/screens/ui_Screen1.c/fonts/ui_font_Font1.c/components） |

## 部署架构

**开发板（RK3568）**跑 `lvglsim` 面板（FBDEV+EVDEV 显示/触摸），**本机 PC** 跑网关 `server`（TCP:8888）。面板作为"面板设备"连到 PC 网关。

## 构建与运行

### PC 调试（SDL 窗口，本机）
```bash
cd smart-gateway-ai && mkdir -p build && cd build
cmake .. && make lvglsim
./bin/lvglsim                        # SDL 窗口（1024×600）
./bin/lvglsim -h 192.168.x.x        # 指定网关(PC)IP
```

### 板子（RK3568，落地）
```bash
# .config 里 CONFIG_LV_USE_SDL 可关（板子用 FBDEV + EVDEV）
mkdir -p build-arm && cd build-arm
cmake -DCMAKE_TOOLCHAIN_FILE=../arm.cmake ..
make lvglsim
# 部署后（PC 网关 IP 用 -h 或 GW_HOST 环境变量指定）：
./bin/lvglsim -b FBDEV -h 192.168.x.x
```

### 网关（本机 PC 上运行 server）
```bash
./server          # 网关（TCP:8888）
```

### 测试目标
```bash
make test_protocol && ./bin/test_protocol   # 协议编解码单测
make test_json_util && ./bin/test_json_util # JSON 工具单测
make mock_server                            # 最小模拟网关（验证面板协议交互）
```

## 一键部署（deploy.sh）

日常改代码后，用 `./deploy.sh` 一条命令完成编译/上传/运行：

```bash
./deploy.sh board       # ★ 编译 ARM + 上传开发板 + 杀旧进程 + 重启（最常用）
./deploy.sh sdl         # 编译并运行 SDL 版面板（自动启动 server）
./deploy.sh sdl-nogw    # 只编译并运行 SDL 面板（不启动 server）
./deploy.sh server      # 编译并重启 PC 网关 server（含模拟数据源）
./deploy.sh arm-only    # 只交叉编译 ARM 版（快速查编译）
./deploy.sh help        # 显示帮助
```

环境变量：

```bash
GW_IP=192.168.1.5 ./deploy.sh board    # 指定网关 IP（默认 10.122.117.172）
GW_PORT=9000 ./deploy.sh server        # 指定端口（默认 8888）
```

> 说明：
> - 改 `src/ui_*.c` / `src/main.c` / `ui/` 字体等面板代码 → 需要 `sdl`（PC 看效果）+ `board`（开发板落地），各编译一次。
> - 改 `src/net_server.c` / `device.c` → `server`（server 只在 PC 跑，不用推开发板）。
> - 改 `.config` / `.config.arm`（Kconfig）→ 需先重新 `cmake` 配置再编译。
> - 开发板 WiFi 断开时（日志 `Network is unreachable`），先执行 `adb shell "sh /usr/bin/wifi_up.sh"`。
> - 开发板后台启动已处理 adb 会话清理问题（`nohup ... & sleep 3` 保持会话），不要手动改成普通 `&`。

## 待办（按顺序）

1. [x] 拷贝 LVGL 核心 + 驱动后端 + 配置，生成 CMakeLists
2. [x] 写 `src/main.c`，SDL 跑通演示界面
3. [x] 从 lv_port_linux 拷贝 SquareLine 导出 UI 到 `ui/`，`ENABLE_SQUARELINE_UI=1`
4. [x] 接入网关协议：`ui_poll_cb` 收帧/发心跳，控件事件回调下发命令（ui_bridge）
5. [ ] 交叉编译，板子落地验证（FBDEV + EVDEV）
6. [ ] 真实网关 server 完整协议对接（当前 smart-gateway 的 server 仅 echo）

## 注意

- **版本统一**：所有 LVGL 相关代码用同一套（当前沿用 lv_port_linux 的 9.x）。
- **PC 运行权限**：SDL 无需权限；FBDEV/EVDEV 需 `video` 组或 root。
- **心跳保活**：面板必须周期发 HEARTBEAT（2s），否则 server 5s 超时踢出。
- **风扇值 bug**：`net_server.c` 的 `on_rule_action` 单字节传输会截断 1800/2400 风扇转速，需改双字节。

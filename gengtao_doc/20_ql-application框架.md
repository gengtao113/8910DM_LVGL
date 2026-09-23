# 20 ql-application 框架

`components/ql-application/` 是移远 OpenCPU 的**客户应用树**。这里编出来的不是内核，是一份独立的 **APPIMG**（`C51_APP.img`），烧进 Flash `0x250000` 起那 2MB。

内核提供 `ql_*` / `osi_*`，应用只通过 export/stub 调用。模型见 [14_OpenCPU开发说明.md](14_OpenCPU开发说明.md)，客户日常改哪见 [12_客户开发说明.md](12_客户开发说明.md)。

> 工程根目录：`8910DM_LVGL`  
> 入口：`init/ql_init.c` → `appimg_enter()`  
> 产物名：`QL_APP_BUILD_VER`（默认 `C51_APP`）

---

## 1. 在整机里的位置

```
底包（不在这棵树里改）              客户应用（本目录）
kernel / hal / driver / modem       ql-application/
8915DM_cat1_open.img                C51_APP.img
ql-kernel/inc/ql_*.h                调 ql_lcd_* / ql_gpio_* / ql_voice_call_*
```

顶层 `components/CMakeLists.txt`：开了 `QL_CCSDK_BUILD` 才编 `ql-application` + `ql-kernel`。  
`init/CMakeLists.txt` 用 `add_appimg_flash_ql_example()` 把 `ql_init.c` 链成 Flash 里的 APPIMG，脚本 `app_flashimg.ld`。

```
内核加载 APPIMG
    └─ appimg_enter()                         init/ql_init.c
          └─ 线程 "ql_init"
                ├─ ql_lvgl_app_init()         本项目真正打开
                ├─ ic_main_entry()            通话回调 + 通话记录
                └─ ql_ledcfg_app_init()       LED 配置（也开着）
                     其余 *_app_init() 都注释掉
```

`ql_init` 线程做完初始化就 `ql_rtos_task_delete` 自己。GUI 活在另一条 `QLVGLDEMO` 线程里，见 [02_启动与任务循环.md](02_启动与任务循环.md)。

---

## 2. 两套东西叠在同一棵树

移远原厂按「一个能力一个 demo 目录」铺的。本手表在上面加了 UI / 触摸 / 通话，**没有拆成另一份工程**。

| 角色 | 目录 | 现在干什么 |
|------|------|------------|
| 产品入口 | `init/` | 决定开哪些功能 |
| 产品 UI | `lv_widgets/` | 表盘、主菜单、页面栈、字库、图 |
| 产品移植 | `lvgl/` | GUI 线程、flush、按键 |
| 产品库 | `lvgl7_lib/` | LVGL 7.9.1 |
| 产品触摸 | `ctp/` | BL6133 |
| 产品业务 | `function/` | 通话回调、通话记录 |
| 官方 demo | `mqtt/` `http/` `sms/` … | 多数**编进镜像但不启动** |
| 旧库 | `lvgl_lib/` | LVGL 6.1.1，V7 开着时不编 |

看代码先分清：目录在、库链上了，不等于线程在跑。开没开看 `ql_init.c` 里那一行 `*_app_init()` 有没有注释。

---

## 3. 目录地图

```
ql-application/
├── CMakeLists.txt                 按 feature 往下 add_subdirectory
├── ql_app_feature_config.cmake    内核能力 → 应用 option
├── ql_app_feature_config.h.in     生成 ql_app_feature_config.h
├── ql_target.cmake                默认内核 feature（项目可覆盖）
│
├── init/                          ★ APPIMG 可执行文件（ql_init.c）
│
├── lvgl/                          ★ ql_app_lvgl：GUI 线程
├── lvgl7_lib/                     ★ lvgl：7.9.1
├── lv_widgets/                    ★ lv_widgets：手表 UI
├── ctp/                           ★ ctp：电容触摸（无 feature 也 add）
├── function/                      ★ function：通话 / 通话记录
├── lcd/                           裸刷 demo（init 里关掉）
│
├── nw/  peripheral/  osi/  dev/  power/   基础库，始终 add
├── fs/  audio/  bt/  sim/  sms/  rtc/ …
├── mqtt/ http/ ftp/ ssl/ socket/ ping/ ntp/ lbs/ …
├── voice_call/ volte/ camera/ gnss/ wifi_scan/ …
├── spi/ spi_flash/ decoder/ virt_at/ http_fota/ LinkSDK/ …
└── tts/                           仅 AUDIO+TTS 时 add（目录可能没有）
```

无条件 `add_subdirectory` 的：`init` `nw` `peripheral` `osi` `dev` `power` `ctp` `function`。  
其余跟 `QL_APP_FEATURE_*` 走，宏关了目录整棵不编。

---

## 4. Feature 怎么裁

三层，缺一层就编不进或链不上内核 API：

```
内核 Kconfig / target.config
    CONFIG_QUEC_PROJECT_FEATURE_MQTT = on      内核里有这份 ql_api
        ↓
ql_app_feature_config.cmake
    QL_APP_FEATURE_MQTT  ON                    才 add mqtt/、才链 ql_app_mqtt
        ↓
ql_app_feature_config.h.in → out/ql_app_feature_config.h
    #define QL_APP_FEATURE_MQTT                C 里才能 #ifdef
        ↓
ql_init.c
    //ql_mqtt_app_init();                      还要在这里取消注释才跑
```

内核关了 `CONFIG_QUEC_PROJECT_FEATURE_*`，cmake 会强制把对应 `QL_APP_FEATURE_*` 设 OFF，避免应用调到不存在的 `ql_*`。

本项目覆盖文件：

| 文件 | 作用 |
|------|------|
| `ql-application/ql_target.cmake` | 仓库默认（EC600U / OpenCPU） |
| `projects/C56U_HM_SH8/ql_target.cmake` | 本手表项目覆盖 |
| `ql_app_feature_config.h.in` | 生成 C 宏（项目目录里也可能有一份） |

LCD 开了会连带默认打开 LVGL + LVGL_V7。CTP 是后挂的：`CONFIG_INCAR_APP_FEATURE_CTP`，目录本身始终编。

更细的开关链见 [05_编译配置与资源.md](05_编译配置与资源.md)。

---

## 5. 一个官方模块长什么样

约定几乎一样，以 MQTT 为例：

```
mqtt/
├── CMakeLists.txt          静态库名 ql_app_mqtt
├── mqtt_demo.c             ql_mqtt_app_init() 里再开一条线程
└── inc/mqtt_demo.h
```

```
set(target ql_app_mqtt)
add_library(${target} STATIC)
target_sources(${target} PRIVATE mqtt_demo.c)
```

`init/CMakeLists.txt` 里对应：

```
if(QL_APP_FEATURE_MQTT)
    target_link_libraries(${target} PRIVATE ql_app_mqtt)
endif()
```

`*_app_init()` 的典型写法：`ql_rtos_task_create(..., ql_xxx_thread, ...)`，自己管自己的线程和回调。  
**编进来只占 Flash，不占线程；调用 init 才占栈和 CPU。**

`peripheral` 是个例外：一个库里塞了 GPIO / ADC / UART / LED / keypad 多个 demo。

---

## 6. 本项目真正在跑的链路

```
C51_APP  (init/ql_init.c)
    │  link
    ├─ ql_app_lvgl          lvgl/lvgl_demo.c
    │     ├─ lvgl           lvgl7_lib
    │     ├─ lv_widgets     表盘 / 菜单 / 页面栈
    │     │     └─ function  ic_main_entry / 通话
    │     └─ ctp            BL6133
    ├─ ql_app_lcd           裸刷 demo 库（线程没开）
    ├─ ql_app_nw / peripheral / osi / dev / sim / power
    └─ 各 QL_APP_FEATURE_* 为 ON 的 ql_app_* demo 库
```

启动后活着的：

```
appimg_enter
  └─ ql_init 线程（干完就删）
        ├─ ql_ledcfg_app_init()
        ├─ ql_lvgl_app_init()  → 线程 QLVGLDEMO
        │       ql_lcd_init
        │       lv_init + flush / keypad / ctp
        │       main_screen()          lv_widgets
        │       10ms → lv_task_handler
        └─ ic_main_entry()
                注册 ql_voice_call 回调
                ic_callog_list_init
```

UI 分层、页面栈见 [04_UI与屏幕管理.md](04_UI与屏幕管理.md)。屏和输入见 [03_显示与输入.md](03_显示与输入.md)。

---

## 7. 产品目录各自干什么

| 库名 | 目录 | 职责 |
|------|------|------|
| （可执行文件） | `init/` | `appimg_enter` / `appimg_exit`，开功能 |
| `ql_app_lvgl` | `lvgl/` | 创建 GUI 线程、注册 display/indev、调 `main_screen` |
| `lvgl` | `lvgl7_lib/` | LVGL 本体 |
| `lv_widgets` | `lv_widgets/` | 手表全部界面和资源；全屏历史见 [21_页面栈.md](21_页面栈.md) |
| `ctp` | `ctp/` | 触摸芯片 → LVGL POINTER |
| `function` | `function/` | `ic_main_entry`、通话事件、通话记录 |
| `ql_app_lcd` | `lcd/` | 不经 LVGL 的刷屏 demo |

`function` 没有 `ql_app_` 前缀，也不是官方 demo。`init` 不直接 `target_link_libraries(function)`，靠 `ql_app_lvgl` → `lv_widgets` → `function` 串进来。`ic_main_entry` 的声明目前靠 `ql_init.c` 里直接调用（实现在 `function/main/ic_mian.c`，文件名少一个 n）。

Ubuntu 模拟器**只编 `lv_widgets` + 少量 function 头文件**，不编整棵 `ql-application`，也不走 `appimg_enter`。见 [19_Ubuntu模拟器.md](19_Ubuntu模拟器.md)。

---

## 8. 想加一个能力怎么走

例如要把 HTTP 从「库在镜像里」变成「真正跑起来」：

1. 确认内核 `CONFIG_QUEC_PROJECT_FEATURE_HTTP=on`（否则 cmake 会把应用侧关掉）。
2. `QL_APP_FEATURE_HTTP` 为 ON（`ql_app_feature_config.cmake` 已按内核能力打开）。
3. 在 `ql_init_demo_thread()` 里取消 `ql_http_app_init()` 的注释，或改成自己的线程。
4. 业务代码调 `ql-kernel/inc/` 里的 `ql_api_http.h`，不要直接碰内核源码。

自己加一个新目录（不是官方 demo）时，最小集合：

```
myfeat/CMakeLists.txt     add_library + target_sources
ql-application/CMakeLists.txt    add_subdirectory_if_exist(myfeat)
init/CMakeLists.txt              target_link_libraries(... myfeat)
init/ql_init.c                   调用你的 init
```

Flash 只有 2MB APPIMG，官方 demo 库开得越多，留给 UI/字库的空间越少。不用的 feature 应在 cmake 里关掉，而不是只注释 init。

---

## 9. 和内核、项目配置的边界

```
projects/C56U_HM_SH8/          本手表的 feature / 头文件覆盖
components/ql-application/     应用源码（本文）
components/ql-kernel/inc/      应用能 #include 的 ql_api
components/ql-kernel/libs/     预编译内核导出库（客户 SDK 形态）
components/apploader/          APPIMG 加载器、ld、stub
```

改屏驱、改分区、改 VoLTE：回底包。  
改表盘、菜单、通话界面、触摸：只动 `ql-application`。

---

## 10. 一句话

`ql-application` 是「官方 OpenCPU demo 仓库 + 本手表产品码」。  
CMake 按内核能力决定**编哪些库**，`ql_init.c` 决定**跑哪些线程**。  
当前产品路径只有：**LVGL 7 + lv_widgets + CTP + function（通话）**，其余目录是可以剪的示例。

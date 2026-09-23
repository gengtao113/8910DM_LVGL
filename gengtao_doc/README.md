# 8910DM LVGL 梳理文档

本目录用于梳理 **Quectel 8910 / EC600U OpenSDK** 上当前 LVGL 相关处理，方便后续改屏、改触控、改 UI、排查刷新/按键问题。

> 工程根目录：`8910DM_LVGL`  
> 产品形态：128×128 儿童手表 / 学生卡类 UI（表盘 + 主菜单 + 通话/通讯录）  
> 当前默认：开启 LCD + LVGL **v7.9.1**

## 文档目录

| 文档 | 内容 |
|------|------|
| [01_架构总览.md](01_架构总览.md) | 目录地图、模块分层、两套 LVGL 库如何选型 |
| [02_启动与任务循环.md](02_启动与任务循环.md) | 从 `ql_init` 到 GUI 线程、`lv_task_handler` |
| [03_显示与输入.md](03_显示与输入.md) | LCD flush、全屏缓冲、按键、CTP 触摸 |
| [04_UI与屏幕管理.md](04_UI与屏幕管理.md) | 表盘/抽屉/主菜单、页面栈、通话联动 |
| [05_编译配置与资源.md](05_编译配置与资源.md) | feature 开关、分辨率、字体/图片/模拟器 |
| [06_已知问题与后续工作.md](06_已知问题与后续工作.md) | 代码里已看到的风险点和建议切入点 |
| [07_SPI_Flash驱动与HAL.md](07_SPI_Flash驱动与HAL.md) | NOR 驱动 / HAL / 型号表，XIP 擦写与写保护 |
| [08_W25Q128JV适配说明.md](08_W25Q128JV适配说明.md) | W25Q128JVSIQ 规格书与现有 drv/HAL 对照 |
| [09_编译说明.md](09_编译说明.md) | Windows/Linux 编译入口、参数与排障 |
| [10_编译产物说明.md](10_编译产物说明.md) | `target/C51` 的 pac / elf / app / prepack |
| [11_分区说明.md](11_分区说明.md) | 当前 8MB OpenCPU 分区表与 APPIMG |
| [12_客户开发说明.md](12_客户开发说明.md) | OpenCPU 客户改哪、怎么编、烧哪个包 |
| [13_内存划分说明.md](13_内存划分说明.md) | 16MB PSRAM / SRAM、AP 4MB 与 APPIMG 128KB |
| [14_OpenCPU开发说明.md](14_OpenCPU开发说明.md) | OpenCPU 模型、加载器、export/stub |

## 30 秒速览

```
ql_init.c
  ├─ ql_lvgl_app_init()     创建 GUI 线程
  └─ ic_main_entry()        通话回调 + 通话记录初始化

ql_lvgl_demo_thread()
  ├─ ql_lcd_init()
  ├─ lv_init()
  ├─ prvLvInitLcd()         flush_cb → ql_lcd_write()
  ├─ prvLvInitKeypad()      keypad → LVGL group
  ├─ ctp_init()             BL6133 中断 → 触摸队列 → POINTER
  ├─ main_screen()          i18n + 待机表盘
  └─ 10ms 定时器循环 lv_task_handler()
```

当前真正跑在设备上的路径是：

- 库：`components/ql-application/lvgl7_lib/`（LVGL 7.9.1）
- 移植/任务：`components/ql-application/lvgl/lvgl_demo.c`
- UI：`components/ql-application/lv_widgets/`
- 触控：`components/ql-application/ctp/`（BL6133）
- LCD 内核 API：`components/ql-kernel/inc/ql_lcd.h`

`lvgl_lib/`（LVGL 6.1.1）和两边的 `lv_port/` 是旧移植残留，**当前 CMake 默认不编进固件**。

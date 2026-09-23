# 19 Ubuntu 上跑手表 LVGL 模拟器

对照 X-TRACK 的 `Software/X-Track/LinuxSDL2`：桌面用 SDL2 开窗口，跑同一套 `lv_widgets`。  
本工程是 **LVGL 7**，显示用仓库里已有的 `lv_drivers/display/monitor.c`，不是 X-TRACK 那套 LVGL 8 `sdl.c`。

只辅助看表盘/菜单布局。通话、CTP、底包 `ql_*` 是空实现，**不能代替板上 APPIMG**。

---

## 1. 依赖

```sh
sudo apt install build-essential cmake pkg-config libsdl2-dev
```

---

## 2. 编译 / 运行

仓库根目录：

```sh
./build_lvgl_sim.sh        # 编译 → simulator/lvgl/LinuxSDL2/watch_sim
./build_lvgl_sim.sh run    # 编译后弹出窗口
./build_lvgl_sim.sh clean
```

窗口逻辑分辨率 **240×240**（和 Windows `LVGL.Simulator` / `lv_conf.h` 一致），默认放大 **2 倍**。  
板上 `CONFIG_LV_GUI_HOR_RES` 仍是 128：数字表盘图是 128，指针盘/拨号等按 240 做的。模拟器跟 VS 工程走 240，不要改成 128，否则菜单会被裁、窗口还会和刷屏步长对不上。

刷屏步长、`LV_HOR_RES_MAX`、SDL 纹理宽度必须同一套数字，否则会花成横条。鼠标点按。关窗口退出。

---

## 3. 和 X-TRACK / Windows 的对应

```
X-TRACK LinuxSDL2/main.cpp          本仓库 LinuxSDL2/main.c
  lv_init()                           lv_init()
  sdl_init() + sdl_display_flush      monitor_init() + monitor_flush
  App_Init()                          main_screen()
  while lv_task_handler               while lv_task_handler
```

| | Windows | Ubuntu |
|--|---------|--------|
| 入口 | `LVGL.Simulator.sln` | `./build_lvgl_sim.sh` |
| 窗口 | Win32drv | SDL2 monitor |
| RTC | `ic_hal_rtc_win32.c` | 同一份 win32 适配（用 libc `time`） |

改 UI 仍改 `components/ql-application/lv_widgets/`，两边模拟器都链这份源码。

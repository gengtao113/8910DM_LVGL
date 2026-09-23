# 8910 手表 UI · Linux SDL2 模拟器

对照 X-TRACK 的 `Software/X-Track/LinuxSDL2`：桌面开 SDL 窗口跑同一套 `lv_widgets`（表盘 / 菜单）。  
本工程是 LVGL 7，显示走 `lv_drivers/display/monitor.c`，不是 LVGL 8 的 `sdl.c`。

通话、CTP、底包 `ql_*` 在 PC 上是空实现，只辅助看布局。

## 依赖

```sh
sudo apt install build-essential cmake pkg-config libsdl2-dev
```

## 编译 / 运行

在仓库根目录：

```sh
./build_lvgl_sim.sh        # 编译
./build_lvgl_sim.sh run    # 编译后弹出 240×240（放大 2 倍）窗口
./build_lvgl_sim.sh clean
```

或：

```sh
cd simulator/lvgl/LinuxSDL2
cmake -S . -B build
cmake --build build -j
./watch_sim
```

用鼠标点按。关窗口即退出。

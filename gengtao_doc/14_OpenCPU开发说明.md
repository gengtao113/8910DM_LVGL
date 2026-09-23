# 14 OpenCPU 是什么、核心怎么实现

本文讲 **OpenCPU 这种开发模型**，以及本工程里它是怎样落到代码上的。  
客户日常改哪、烧哪个包见 [12_客户开发说明.md](12_客户开发说明.md)。  
Flash 分区见 [11_分区说明.md](11_分区说明.md)，RAM 窗口见 [13_内存划分说明.md](13_内存划分说明.md)。

> 工程根目录：`8910DM_LVGL`  
> 开关：`CONFIG_QL_OPEN_EXPORT_PKG`（`quec_proj_config.h`）  
> 宏别名：`__QUEC_VER_EXPORT_OPEN_CPU__`（`QuecPrjName.h`）

---

## 1. 一句话

**OpenCPU = 把客户应用编成单独一份镜像（APPIMG），烧进独立 Flash 分区，开机后由内核加载并当普通函数调用。**  
客户不链内核源码，只通过一份导出表调用 `ql_*` / `osi_*` / `malloc`。不是 Linux 用户态，也没有进程级隔离。

```
Flash 8MB NOR（XIP @ 0x60000000）
0x000000                                                     0x800000
|< BOOT >|<---- APP 内核 ---->|<-- APPIMG 客户 -->|<FSYS>|< FMOD >|<FFAC>|
  64KB         2.25MB                2MB            320KB  3.25MB  128KB
                 │                     │
                 │ 加载 + 填 stub      │ 代码 XIP，不搬
                 ▼                     ▼
PSRAM 尾部 4MB（AP，VoLTE）
0x80C00000              0x80FA0000     0x80FC0000     0x81000000
|<-- 内核 ram 3.625MB -->|< APP 128KB >|<  BT 256KB >|
   实现 ql_* / 堆          stub/.data/.bss
```

---

## 2. 和标准 AT 固件差在哪

移远 8910 同一套 SDK 能出两种产品：

| | 标准固件（AT） | **OpenCPU（本项目）** |
|--|----------------|------------------------|
| 宏 | 无 `CONFIG_QL_OPEN_EXPORT_PKG` | `CONFIG_QL_OPEN_EXPORT_PKG` |
| 客户怎么用模组 | UART 发 AT | C 代码直接调 `ql_*` |
| 客户代码在哪 | 主机 MCU / PC | 模组 AP 上的 APPIMG |
| 应用和内核 | 一体，或根本没有客户镜像 | **两份 ELF、两份链接脚本** |
| 升级 | 整包 | 底包可不动，只换 `C51_APP.pac` |

标准固件里，业务在 MCU，模组当「带 AT 的猫」。  
OpenCPU 把 MCU 省掉：UI、通话、触控都跑在模组 Cortex-A5 上，和内核**同核、同地址空间、同特权级**。

```
标准 AT                              OpenCPU（本项目）
┌──────────┐  AT 指令  ┌──────────┐   ┌────────────────────────────────┐
│ 主机 MCU │──────────►│  模  组  │   │           模组 AP              │
│  业务/UI │           │ 内核+CP  │   │  ┌──────────┐  stub   ┌─────┐ │
└──────────┘           └──────────┘   │  │ 客户APP  │────────►│内核 │ │
     两颗 MCU，串口对话                 │  │ APPIMG   │  ql_*   │+CP  │ │
                                       │  └──────────┘         └─────┘ │
                                       │     同一颗 A5，函数调用        │
                                       └────────────────────────────────┘
```

---

## 3. 核心是三件事

1. **两份独立镜像**：内核链 `flashrun.ld`，应用链 `app_flashimg.ld`。  
2. **导出 / 跳板**：内核把允许调用的函数做成表；应用链进 `core_stub.o`，加载时把跳板改成真实地址。  
3. **加载器**：内核读 APPIMG 头，校验、搬 `.data`、清 `.bss`、填 stub，再调 `appimg_enter()`。

没有系统调用、没有动态链接器、没有独立堆管理器。ABI 就是那张导出表 + 镜像头。

```
                    ┌─ ① 两份镜像 ─────────────────────────┐
 Flash 8MB          │  BOOT │  APP 内核  │ APPIMG 客户 │ … │
                    └───────┴────────────┴─────────────┴───┘
                                      │
                    ┌─ ② 导出 / 跳板 ─┴────────────────────┐
                    │  内核: core_export.o   真实地址表     │
                    │  应用: core_stub.o     带 tag 的跳板  │
                    │  加载时按 tag 把跳板改成 BL 到内核    │
                    └──────────────────┬───────────────────┘
                                      │
                    ┌─ ③ 加载器 ──────┴────────────────────┐
                    │  读头 → 校验 → 搬 data / 填 stub      │
                    │  → 调用 appimg_enter()                │
                    └──────────────────────────────────────┘
```

---

## 4. 开机后谁把应用拉起来

内核入口在 `components/appstart/src/app_start.c`。

```
时间 →

 复位     BOOT          内核 APP                         客户 APPIMG
  │        │              │                                   │
  ▼        ▼              ▼                                   ▼
 ┌──┐    ┌────┐    ┌──────────────┐                    ┌────────────┐
 │上电│──►│boot│──►│ osiAppStart  │                    │ 还在 Flash │
 └──┘    └────┘    │  挂 FS/NVM   │                    │ 尚未执行   │
                   │  GPIO/USB/PM │                    └────────────┘
                   │  quec_startup│── 等 CP / 网络 ──┐
                   └──────────────┘                   │
                                      quec_app_start  │
                                      ┌───────────────▼───────────────┐
                                      │ appImageFromMem(0x60250000)   │
                                      │   校验头 / 搬段 / 填 stub     │
                                      │ gAppImgFlash.enter(NULL)      │
                                      └───────────────┬───────────────┘
                                                      │
                                                      ▼
                                               appimg_enter()
                                               创建 ql_init 任务
                                               （入口立刻返回）
```

`quec_app_start()` 只在 OpenCPU 包里编进去：

```c
#ifdef CONFIG_QL_OPEN_EXPORT_PKG
void quec_app_start(void)
{
    const void *flash_img_address = (const void *)CONFIG_APPIMG_FLASH_ADDRESS;
    if (appImageFromMem(flash_img_address, &gAppImgFlash))
        gAppImgFlash.enter(NULL);
}
#endif
```

本项目 `CONFIG_APPIMG_LOAD_FLASH=y`，`CONFIG_APPIMG_FLASH_ADDRESS=0x60250000`（物理 `0x250000`）。  
`CONFIG_APPIMG_LOAD_FILE` 关着：应用**不从文件系统再搬一份**，就在 NOR 上 XIP。

`appimg_enter()` 在 `ql-application/init/ql_init.c`：跑 C++ 全局构造，再 `ql_rtos_task_create("ql_init", …)`。  
入口本身必须很快返回，真正的 UI / 通话在后续任务里。

---

## 5. APPIMG 长什么样

链接脚本：`components/apploader/pack/app_flashimg.ld`。  
入口符号：`appimg_enter` / `appimg_exit`。

镜像头固定 **128 字节**，魔数 `'APP2'`（`0x41505032`）：

| 字段 | 作用 |
|------|------|
| magic / image_size / image_crc | 识别和完整性 |
| stub_version | 必须和内核 `gCoreExportVersion` 主版本一致 |
| enter / exit / get_param / set_param | 函数地址 |
| 最多 6 个 section | type + Flash 偏移 + 长度 + RAM 目的地 |

本项目各段：

| type | 名字 | 加载动作 |
|------|------|----------|
| 2 STUB | `.corestub` | 拷到 `0x80FA0000`，再按导出表改写成跳转 |
| 4 XIP | `.text` / `.init_array` | **不拷**，在 `0x60250000` 就地执行 |
| 1 COPY | `.data` | `memcpy` 到客户 RAM |
| 3 CLEAR | `.bss` | `memset` 0 |

Flash 上整份镜像（从 `0x60250000` 起，最多 2MB）：

```
0x60250000
┌──────────────────────────────────────────────────────────┐
│ 128B 头  APP2 | size | crc | stub_ver | enter/exit | 段表 │
├──────────┬───────────────────────────────────────────────┤
│ STUB 载荷│  给 .corestub 用的原始跳板（还没改写成地址）   │
├──────────┼───────────────────────────────────────────────┤
│ DATA 载荷│  .data 初值（加载时 memcpy 到 RAM）            │
├──────────┼───────────────────────────────────────────────┤
│ TEXT XIP │  代码 / 只读数据 / init_array                  │
│          │  不拷贝，CPU 直接从 NOR 取指                   │
└──────────┴───────────────────────────────────────────────┘
0x60450000（APPIMG 区尾，后面是 FSYS）
```

加载后 Flash 和 RAM 的对应：

```
Flash APPIMG 2MB                         客户 RAM 128KB
0x60250000                               0x80FA0000
┌─────────────┐                          ┌─────────────────┐
│ 头 128B     │                          │ .corestub       │  ← STUB：拷入再改写
├─────────────┤   memcpy + 改写跳板      │  ql_lcd_write:  │
│ stub 载荷   │ ───────────────────────► │   B  0x80Cxxxxx │
├─────────────┤   memcpy                 ├─────────────────┤
│ data 载荷   │ ───────────────────────► │ .data           │
├─────────────┤   memset 0               ├─────────────────┤
│             │ ───────────────────────► │ .bss            │
│ .text XIP   │  不搬，就地取指          └─────────────────┘
│ appimg_enter│                          0x80FC0000（BT 起）
└─────────────┘
0x60450000
```

`dtools mkappimg` 把 ELF 收成 `C51_APP.img`（填 CRC）。可选签名。  
加载实现：`components/apploader/src/app_loader.c`。

校验失败（魔数、CRC、签名、入口越界、段落到内核/BT 窗口、stub 版本不对）就**不调 enter**，机器还能起，只是没有 UI。

---

## 6. 客户怎么调到内核函数（最核心的一层）

应用**不能**直接链 `ql-kernel` 的 `.a` 当普通库用完就完（那些库是给内核自己编的）。  
两份镜像地址空间约定好了，但链接时彼此看不见对方的符号。解决办法是 **export / stub**：

```
                    core_export.list   （同一份名单，@1.0）
                    ql_lcd_write
                    ql_rtos_task_create
                    malloc
                           │
              ┌────────────┴────────────┐
              ▼                         ▼
     dtools expgen --export    dtools expgen --stub
              │                         │
              ▼                         ▼
     core_export.o               core_stub.o
     链进内核 APP                链进客户 APPIMG
     ┌─────────────────┐         ┌──────────────────┐
     │ tag=ql_lcd_write│         │ ql_lcd_write:    │
     │  → 0x80C0A100   │         │   insn + tag     │  ← 还不是真实地址
     │ tag=malloc      │         │ malloc:          │
     │  → 0x80C1B200   │         │   insn + tag     │
     └─────────────────┘         └──────────────────┘
```

名单：`components/apploader/src/core_export.list`。  
版本写在文件头 `@1.0`。OpenCPU 段用 `#ifdef CONFIG_QL_OPEN_EXPORT_PKG` 包住，里面才是 `ql_lcd_*`、`ql_rtos_*`、`ql_voice_call_*` 等。

加载时 `appImageLoadStub()`（也在 `core_export.o` 里）把跳板改成真地址：

```
加载前（Flash 里的 stub 载荷）          加载后（RAM 0x80FA0000）
┌──────────────────────────┐            ┌──────────────────────────┐
│ ql_lcd_write:            │            │ ql_lcd_write:            │
│   insn                   │  查 tag    │   B   0x80C0A100         │
│   tag = "ql_lcd_write"   │ ─────────► │                          │
└──────────────────────────┘            └────────────┬─────────────┘
                                                     │  CPU 执行 BL
                                                     ▼
                                            内核 0x80C0A100
                                            ql_lcd_write()
                                                 │
                                                 ▼
                                            drv_lcd_…  刷屏
```

之后应用里写 `ql_lcd_init()`，实际是 **BL 到内核里已经存在的实现**。  
`malloc` / `free` 也在这张表上，所以客户堆就是内核堆。

```
客户 .c                    客户 RAM stub              内核 ram
lvgl_demo.c                0x80FA0xxx                 0x80Cxxxxx
┌──────────────┐          ┌──────────────┐           ┌──────────────┐
│ ql_lcd_write │──调用──►│ 已改写的跳板 │──BL────►│ 真正的实现   │
│ (buffer, …)  │          │ B  内核地址  │           │ → 屏驱       │
└──────────────┘          └──────────────┘           └──────────────┘
```

新增一个给客户用的 API，必须：内核里实现 → 写进 `core_export.list` → 重编内核和应用。只改应用链不过或加载后跳飞。

---

## 7. 运行时还是「一个程序」

| 有的 | 没有的 |
|------|--------|
| 独立 Flash 分区、独立链接、独立下载槽 | 独立进程 / 独立页表 |
| 加载时地址检查 | MMU 挡客户写内核 |
| stub 版本检查 | 系统调用门 |

客户野指针可以打到内核堆。`ql_rtos_task_create` 出的栈、LVGL 全屏缓冲，都从内核 `malloc` 来。  
隔开的只是 **客户 `.data/.bss` 那 128KB**，细节见 [13 §4](13_内存划分说明.md)。

```
同一套 Cortex-A5 页表（AP 这 4MB 全可写）

0x80C00000                         0x80FA0000     0x80FC0000
┌──────────── 内核 3.625MB ────────┬── APP 128KB ─┬── BT 256KB ─┐
│ ttbl / .data / .bss              │ stub         │ 蓝牙固件    │
│                                  │ .data .bss   │             │
│ ════════ 堆 ≈ 3.24MB ════════    │              │             │
│   内核 malloc                    │              │             │
│   客户 malloc  ←── 同一片 ───┐   │              │             │
│   ql_rtos 任务栈             │   │              │             │
│   LVGL 全屏缓冲 32KB         │   │              │             │
└──────────────────────────────┘   └──────────────┴─────────────┘
        ▲ 野指针可以从这里打进去
        │
        └── 客户代码（XIP 在 Flash）跑在同一特权级
```

---

## 8. 编译时两边怎么走

```
                         同一棵树、同一次 ninja
                                    │
                 ┌──────────────────┴──────────────────┐
                 ▼                                     ▼
        内核 8915DM_cat1_open                    应用 C51_APP
        kernel / hal / driver                    ql-application/*
        ql-kernel/libs/*.a                       + core_stub.o
        + core_export.o                          （不链内核 .o）
                 │                                     │
                 ▼                                     ▼
        8915DM_cat1_open.img                     C51_APP.img
                 │                                     │
                 ▼                                     ▼
        底包 C51.pac                             C51_APP.pac
        （APP 槽 = 内核）                        （只有 APPIMG 槽）
                 │                                     │
                 └──────────── pacmerge ───────────────┘
                                    │
                                    ▼
                    8915DM_cat1_open_C51_merge.pac
                    APP 槽 = 内核    APPIMG 槽 = 这次 UI
```

`add_appimg()`（`cmake/extension.cmake`）强制把 `core_stub.o` 加进应用可执行文件。  
`init/CMakeLists.txt` 用 `add_appimg_flash_ql_example(C51 ql_init.c)`，再 `target_link_libraries` 链 `ql_app_lvgl` 等**应用侧**库。

客户日常只改 `ql-application/`，产物是 APPIMG。换 API 表、换分区、换驱动，必须重出底包。

---

## 9. 本手表落到这条链上的位置

```
quec_app_start
        │
        ▼
 appimg_enter()                         ql_init.c
        │  立刻返回
        ▼
 ql_init 任务
        │
        ├──────────────┐
        ▼              ▼
 ql_lvgl_app_init   ic_main_entry
   创建 GUI 线程      通话回调 / 记录
        │
        ▼
 QLVGLDEMO 线程
        │
        ├─ ql_lcd_init          ──stub──► 内核 LCD
        ├─ lv_init / flush_cb
        ├─ keypad / ctp
        ├─ main_screen()        表盘
        └─ 10ms 循环 lv_task_handler
```

`ql_*` 头文件在 `components/ql-kernel/inc/`，实现在预编译 `ql-kernel/libs/` 和开源 `driver/`。  
应用只 `#include` 头文件，链接靠 stub。

最小官方样例：`components/apploader/pack/hello_world.c`（`appimg_enter` + 一个 `osiThreadCreate`）。本项目只是把 hello world 换成了 LVGL 手表。

---

## 10. 和客户开发的对应

| 客户感知 | 底层对应 |
|----------|----------|
| 改 `ql-application/`，只烧 `C51_APP.pac` | 只换 APPIMG，加载器仍调同一个 enter |
| 调用 `ql_lcd_*` / `ql_rtos_*` | stub → 内核导出表 |
| 全局变量不能太大 | `app_flashimg.ld` 的 128KB ram |
| `malloc` 很大一块能成功 | 实际吃内核堆 |
| 底包和 APP 版本对不齐 | stub 主版本不一致则加载失败 |
| 新板必须 merge.pac | 内核 + 分区表 + APPIMG 必须一套 |

---

## 11. 一句话（实现向）

OpenCPU 的核心不是另做一套操作系统，而是：**内核先起来，按约定从 Flash `0x60250000` 加载第二份镜像，用 export/stub 把 `ql_*` 接到内核实现，再调用 `appimg_enter`。** 客户开发就是写这份镜像。

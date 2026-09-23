# 17 `core_export.list` 与 OpenCPU 原理

文件：`components/apploader/src/core_export.list`  
相关实现：`apploader/CMakeLists.txt`、`app_loader.c`、`app_flashimg.ld`、`dtools expgen`

总述见 [14_OpenCPU开发说明.md](14_OpenCPU开发说明.md)。本文只盯**导出名单**本身：它怎么变成 ABI。  
改名单前的十条注意见 [18_core_export注意事项.md](18_core_export注意事项.md)。

---

## 1. OpenCPU 在这一层到底是什么

内核和应用是**两份独立镜像**，链接时互相看不见符号。客户写 `ql_lcd_write()`，链接器必须找到这个符号；内核里也有同名实现，但地址要等开机加载时才能填进去。

`core_export.list` 就是双方共同承认的那张**函数白名单**。没有系统调用、没有动态链接器。ABI = 这份名单 + APP2 镜像头。

```
        ┌────────────── core_export.list ──────────────┐
        │  @1.0                                        │
        │  malloc / ql_lcd_write / ql_rtos_task_create │
        └───────────────────┬──────────────────────────┘
                            │ 同一份名单，两种产物
              ┌─────────────┴─────────────┐
              ▼                           ▼
     客户 APPIMG                    内核 APP（底包）
 ┌────────────────────┐          ┌────────────────────┐
 │ ql-application/*.c │          │ ql_* / osi_* 实现  │
 │      +             │          │      +             │
 │   core_stub.o      │          │  core_export.o     │
 │  跳板 + 名字tag    │          │  tag → 真地址      │
 └─────────┬──────────┘          └──────────┬─────────┘
           │  开机 appImageLoadStub         │
           │  按 tag 焊死跳转               │
           └──────────────┬─────────────────┘
                          ▼
                   客户调用 ql_lcd_write()
                   实际跑到内核实现
```

开机路径（本项目）：

```
时间 →

 复位    BOOT         内核 APP                         客户 APPIMG
  │       │              │                                  │
  ▼       ▼              ▼                                  ▼
 ┌──┐   ┌────┐   ┌──────────────┐                    ┌────────────┐
 │上电│─►│boot│─►│ osiAppStart  │                    │ 还在 Flash │
 └──┘   └────┘   │ quec_app_start│                   │ 尚未执行   │
                 └──────┬───────┘                    └─────┬──────┘
                        │                                  │
                        │  appImageFromMem(0x60250000)     │
                        │◄──────── 读 APP2 头 ─────────────┤
                        │  校验 CRC / stub 版本            │
                        │  搬 .data、清 .bss               │
                        │  按 tag 填 stub ────────────────►│ RAM 0x80FA0000
                        │                                  │
                        │  gAppImgFlash.enter(NULL)        │
                        └──────────────┬───────────────────┘
                                       ▼
                                appimg_enter()   ← ql_init.c
```

`CONFIG_QL_OPEN_EXPORT_PKG=y` 时才编 `quec_app_start()`。关掉就不再加载 APPIMG。

---

## 2. 名单怎样变成两个 `.o`

`core_export.list` **不是纯文本名单**，先当 C 做预处理（`cpp_only`：`-E -P -x c`），再交给 `dtools expgen`：

```
core_export.list
  #include "hal_config.h" …
  @1.0
  ql_lcd_write
  malloc
        │
        ▼  gcc -E（宏生效，#ifdef 裁掉）
预处理后的符号表
        │
        ▼  dtools expgen -p 8910
   ┌────┴────┐
   ▼         ▼
core_export.o     core_stub.o
链进内核          链进 APPIMG
gCoreExportVersion
appImageLoadStub()
```

CMake：`components/apploader/CMakeLists.txt`。  
`add_appimg()`（`cmake/extension.cmake`）强制把 `core_stub.o` 链进应用。  
客户 SDK 不再跑 expgen，直接用打包进去的那份 `sdk_prebuilt/core_stub.o`。

### 2.0 stub 更强调什么

英文 *stub* 在这里就是**桩、占位、替身**。它强调的不是「函数怎么实现」，而是：

| 强调 | 含义 |
|------|------|
| **有名字，能链上** | 应用写 `ql_lcd_write()`，链接器必须找到这个符号，否则编不过 |
| **没有函数体** | stub 里没有刷屏、没有 malloc 算法，只有 8 字节跳板 |
| **真活在别处** | 实现在内核镜像里；stub 只负责「先占个坑，开机再接过去」 |
| **可被改写** | 后 4 字节先放 tag（我是谁），焊死后改成真地址（去哪执行） |

一句话：**stub 管「叫得出这个名字」，不管「这个名字干什么」。**

```
客户源码里看起来像在调一个普通函数：

    ql_lcd_write(buf, x1, y1, x2, y2);

编译 / 链接时实际链到的是 stub 里的同名符号，不是内核 .c：

    你以为的                          实际链到的
 ┌─────────────────┐              ┌──────────────────────────┐
 │ ql_lcd_write()  │              │ stub:  ql_lcd_write      │
 │   刷屏实现      │     ✗        │   ldr pc + tag           │
 │   （内核里才有）│              │   没有一行刷屏代码       │
 └─────────────────┘              └────────────┬─────────────┘
                                               │ 开机焊死后
                                               ▼
                                      内核里真正的 ql_lcd_write()
```

和旁边那个 `core_export.o` 对照：

```
                同一份名单上的同一个名字
                         │
          ┌──────────────┴──────────────┐
          ▼                             ▼
   stub（客户侧）                 export（内核侧）
   强调：替身、占坑               强调：真身、目录
   「先让你链得过」               「这是真地址表」
   8 字节跳板 + tag               tag + 函数指针
   链进 APPIMG                    链进底包
```

没有 stub，客户工程会在链接期报 `undefined reference to ql_lcd_write`——不是运行时才发现。  
没有 export，开机焊的时候 `bsearch` 对不上，整份 APPIMG 拒载。  
所以 stub 解决的是**编译期的符号**，export 解决的是**加载期的地址**。

每条 stub 固定 **8 字节**：一条 `ldr pc, [pc]`，后面跟名字算出来的 32 位 tag。  
内核表 `gCoreApiTable` 是 `{tag, 函数地址}`，**按 tag 排序**，加载时 `bsearch`。匹配靠名字，不靠名单行号。

```
一条 stub（8 字节）                    内核 gCoreApiTable（按 tag 排序）
┌──────────┬──────────┐               ┌──────────┬────────────────┐
│ ldr pc   │ tag      │               │ tag      │ 函数地址       │
│ [pc]     │ 名字hash │               │ 0x0096…  │ ql_spi_nor_…   │
└──────────┴──────────┘               │ 0x4362…  │ ql_lcd_write   │  ← 本机实测
                                      │ 0xe3c5…  │ malloc         │
  ql_lcd_write 的 tag                 └──────────┴────────────────┘
  永远是 0x43621e41                          ▲
       │                                     │
       └──────── bsearch(tag) ───────────────┘
                 找到就把「函数地址」写回 stub 后 4 字节
                 下次 CPU 执行 ldr pc 就跳进内核
```

加载前 / 加载后（同一条 `ql_lcd_write`）：

```
Flash 里的原始跳板                      RAM 0x80FA0xxx 填完之后
┌──────────┬──────────┐                ┌──────────┬──────────────┐
│ ldr pc   │ 43621e41 │   查到地址     │ ldr pc   │ 0x80C0A100   │
│          │ (tag)    │ ─────────────► │          │ 真入口       │
└──────────┴──────────┘                └────┬─────┴──────────────┘
                                            │  CPU 取指
                                            ▼
                                     内核 ql_lcd_write()
                                     刷屏
```

### 2.1 tag 是什么，开机怎样「焊死」

tag **不是**函数指针，也**不是**名单行号。它是 `expgen` 用函数**名字字符串**算出来的 32 位指纹（本机实测）：

```
名字字符串                算出的 tag（永远跟名字走）
"ql_lcd_write"        →  0x43621e41
"malloc"              →  0xe3c52ebf
"ql_rtos_task_create" →  0xa8024c90
"osiThreadCreate"     →  0xa2c378d7
```

同一条函数在客户 stub 里和内核表里各存一份**相同的 tag**，当作配对钥匙：

```
客户 RAM 里还没焊的 stub              内核里的导出表（按 tag 排序，给 bsearch 用）
地址 0x80FA0100                       gCoreApiTable[]
┌──── 8 字节 ────┐                    ┌─ tag ────┬─ 真地址 ────┐
│ +0  ldr pc,[pc]│                    │ 00969295 │ ql_spi_nor… │
│ +4  43621e41   │◄── 同一把钥匙 ──►  │ 43621e41 │ 80C0A100    │  ql_lcd_write
│     ↑ tag      │                    │ a8024c90 │ 80C1B200    │  ql_rtos_…
└────────────────┘                    │ e3c52ebf │ 80C01234    │  malloc
                                      └──────────┴─────────────┘
  后 4 字节现在是「我是谁」              后 4 字节已经是「去哪执行」
```

开机 `appImageLoadStub` 对**每一条** stub 做一次查找，找到就把「去哪执行」盖到 tag 上面——盖完 tag 就没了，只剩地址。这就是「焊死」：

```
步骤 1  从 stub 取出 tag
        0x80FA0104 里读到  43621e41

步骤 2  拿这个数去内核表做 bsearch（二分，O(log N)）
                    表已按 tag 从小到大排好
                         │
              ┌──────────┼──────────┐
              ▼          ▼          ▼
           0096…      43621e41    e3c5…
              偏小        命中        偏大
                         │
                         ▼
                    这一行的真地址 = 0x80C0A100

步骤 3  焊：把 stub 后 4 字节从 tag 改写成真地址
        焊前  [ ldr pc ] [ 43621e41 ]     「我叫 ql_lcd_write」
        焊后  [ ldr pc ] [ 80C0A100 ]     「直接去内核这个地址」
                         ^^^^^^^^
                         tag 被盖掉，以后再也不查表
```

焊完之后，客户每次调用都不再认名字、也不再 bsearch，只是普通跳转：

```
手表 UI 里写的：
    ql_lcd_write(buf, x1, y1, x2, y2);
            │
            │  编译时链到 stub 符号 ql_lcd_write
            ▼
    0x80FA0100:   ldr pc, [pc]     从后面 4 字节取地址，赋给 PC
    0x80FA0104:   80C0A100         ← 开机焊进去的，不是 tag 了
            │
            ▼
    0x80C0A100:   内核真正的 ql_lcd_write()
                  往 LCD 刷这一块
```

对照三句话：

| 说法 | 实际含义 |
|------|----------|
| tag | 名字的指纹，只在**加载那一瞬间**用来对钥匙 |
| 焊死 | 把指纹换成内核地址，写进 RAM 里那条 stub |
| 之后每次调用 | CPU 只看焊好的地址，不再看名单、不再比 tag |

所以调换 `core_export.list` 行序不影响：钥匙是名字算出来的 `43621e41`，不是「第 2 行」。名单里改名 / 删掉，钥匙对不上，`bsearch` 失败，整份 APPIMG 拒载。

---

## 3. 文件结构（对照 1–1025 行）

### 3.1 头文件和版本

```
#include "hal_config.h"
#include "drv_config.h"
#include "audio_config.h"
#include "quec_proj_config.h"

@1.0          ← 写成 gCoreExportVersion，当前 1.0
```

`@主.次` 是 ABI 版本，不是注释。加载器规则（`app_loader.c`）：

```
APPIMG 头 stub_version          内核 gCoreExportVersion
        │                              │
        └──────── 比较 ────────────────┘

  MAJOR 不同          MINOR(应用) > MINOR(内核)       MAJOR 同 且 应用MINOR ≤ 内核
 ┌────────────┐      ┌─────────────────────┐      ┌──────────────────────────┐
 │ 拒载       │      │ 拒载                 │      │ 通过                     │
 │ 删/改语义  │      │ 新 SDK 刷了旧底包    │      │ 旧应用跑在只追加过       │
 │ 必须重发APP│      │ 底包还不认识新 API   │      │ 符号的新底包上           │
 └────────────┘      └─────────────────────┘      └──────────────────────────┘
```

| 条件 | 结果 |
|------|------|
| 应用 MAJOR ≠ 内核 MAJOR | **拒绝加载** |
| 应用 MINOR > 内核 MINOR | **拒绝加载**（新 SDK 跑在旧底包上） |
| 应用 MINOR ≤ 内核 MINOR，MAJOR 相同 | 通过（旧应用可以跑在只加过符号的新底包上） |

### 3.2 两层开关，不要看错方向

本项目 `ql_target.cmake` / `quec_proj_config.h` 同时开了：

- `CONFIG_QUEC_PROJECT_FEATURE` —— 移远工程
- `CONFIG_QL_OPEN_EXPORT_PKG` —— OpenCPU 导出包

因此预处理之后：

| 包裹 | 本项目结果 | 含义 |
|------|------------|------|
| 无条件：`osi*`、`malloc`/`free`、`lwip_*`、`mbedtls_*` | **导出** | 客户能直接调 OSI / 堆 / socket / TLS |
| `#ifndef CONFIG_QUEC_PROJECT_FEATURE`：`vfs_*`、`drvLcd*`、`drvUart*`、`drvCam*`、`auPlayer*` | **不导出** | 移远客户不要走芯片原厂裸接口 |
| `#ifdef CONFIG_QL_OPEN_EXPORT_PKG`：`ql_*` | **导出**（再受各 `CONFIG_QUEC_PROJECT_FEATURE_xxx` 裁） | 客户该用的公开 API |

也就是说：手表应用调 `ql_lcd_write`、`ql_fopen`、`ql_voice_call_start`；**不要**指望 `drvLcdBlockTransfer`、`vfs_open` 出现在 stub 里。那些符号只在非移远工程才导出。

```
core_export.list 原文（1025 行，带 #ifdef）
        │
        ▼  gcc -E
┌───────────────────────────────────────────────────────────┐
│ 无条件留下                                                 │
│   osi*  malloc/free  lwip_*  mbedtls_*                    │
│                                                           │
│ #ifndef QUEC_PROJECT          本项目已定义 → 整段丢掉      │
│   vfs_*  drvLcd*  drvUart*  auPlayer*                     │
│                                                           │
│ #ifdef QL_OPEN_EXPORT_PKG     本项目已开 → 留下            │
│   ql_lcd_*  ql_rtos_*  ql_fopen …                         │
│   再套一层 FEATURE_HTTP / MQTT / VOICE_CALL …             │
│   feature 关着的 ql_* 再丢掉                              │
└───────────────────────────────────────────────────────────┘
        │
        ▼
  真正进 stub / export 的符号集合
```

### 3.3 本项目实际能调到的大类

无条件（内核一直在）：

- 跟踪 / OSI 线程锁定时器 / fifo / pipe / 临界区
- `malloc` `calloc` `realloc` `free` `memalign` —— **客户堆就是内核堆**
- `lwip_*`（再加 `CONFIG_QL_OPEN_EXPORT_PKG` 下的 `ip4addr_*` 等）
- `mbedtls_*`

OpenCPU `ql_*`（再按 feature）：

| 段 | 典型符号 | 本产品开关 |
|----|----------|------------|
| LCD | `ql_lcd_init` / `ql_lcd_write` … | LCD on |
| 网络 / 拨号 | `ql_nw_*` `ql_start_data_call` | 开 |
| GPIO / OSI | `ql_gpio_*` `ql_rtos_*` | 开 |
| HTTP/FTP/MQTT/SSL/PING/NTP | `ql_httpc_*` 等 | 对应 feature on |
| 设备 / SIM / 文件 / 电源 | `ql_dev_*` `ql_sim_*` `ql_fopen` `ql_power_*` | 开 |
| UART / SMS / 语音 / VoLTE | `ql_uart_*` `ql_sms_*` `ql_voice_call_*` `ql_volte_*` | 开 |
| SPI / SPI NOR | `ql_spi_*` `ql_spi_nor_*` | 看 feature |
| 音频 / 按键 / RTC / 充电 | `ql_aud_*` `ql_keypad_*` `ql_rtc_*` | 看 feature |
| BT / GNSS / Camera / I2C | `ql_bt_*` … | 看 feature |

`#ifdef CONFIG_QUEC_PROJECT_FEATURE_HTTP` 这类关了，对应 `ql_*` **不会进 stub**。应用里若仍调用，链接报 undefined reference，不是运行时才挂。

---

## 4. 注意事项

十条注意（每条带图）已独立成篇，改 `core_export.list` 前先看：  
[18_core_export注意事项.md](18_core_export注意事项.md)。

提要：名字必须在内核里且能过预处理；改名单要重编底包并重打 SDK；只追加升 MINOR，删/改语义升 MAJOR；只调行序不影响；客户改 list 无效。

---

## 5. 和日常开发怎么对应

```
方案内部                              客户
改 core_export.list                   不能改 stub
./build_all.sh                        ./build_app.sh
  → 新底包 + 新 core_stub.o             → 只链现有 stub
./pack_opencpu_sdk.sh                 QFlash 只下 C51_APP.pac
烧整机底包（方案）                     只覆盖 APPIMG
```

```
内部一次 expgen
        │
        ├──────────── core_export.o ──► 链进底包 ──► 烧整机
        │
        └──────────── core_stub.o  ──► pack 进 SDK
                                           │
                                           ▼
                                    客户 ./build_app.sh
                                    链进 C51_APP.pac
                                           │
                              机器上底包 ←─┴─► 客户 APPIMG
                              必须同一对 stub/export
```

底包和 SDK 对不齐时（加载失败，内核仍起、没有 UI）：

```
新 SDK（@1.1，多了 ql_foo）    旧底包（@1.0，没有 ql_foo）
        │                              │
        └──── MINOR(应用) > MINOR(核) ─┘   → 拒载

旧 APP（@1.0）                 新底包（@1.1，只追加）
        │                              │
        └──── MINOR 更小，tag 都在 ────┘   → 可通过

删了 ql_bar 还自称 @1.0        旧 APP 仍调 ql_bar
        │                              │
        └──── bsearch 找不到 tag ──────┘   → 拒载
```

本手表 UI 调到内核的路径，全部经过这张表里的 `ql_*`（以及 `malloc` / `osi*`），**没有**再走 `appimg_get_param` / `set_param` 那套 dispatch。那两个入口在链接脚本里 `PROVIDE(... = 0)`，当前没用。

新增给客户的 API 检查清单：

1. 内核里已有实现，且是全局 C 符号  
2. 名字写进 `core_export.list`，放对 `#ifdef`  
3. 如只是追加：考虑把 `@1.0` 改成 `@1.1`  
4. `./build_all.sh`，确认 `core_stub.o` 更新  
5. `./pack_opencpu_sdk.sh` 再发给客户  
6. 客户侧 `#include` 对应 `ql_*.h`，不要直接 `#include` 内核私有头

---

## 6. 一句话

`core_export.list` 是 OpenCPU 的 ABI 契约：预处理后的函数名做成内核地址表和应用跳板，开机按 tag 焊死。客户开发就是写 APPIMG，只能调用这张表上还在的符号；改表等于改底包，必须方案重编并重发 SDK。

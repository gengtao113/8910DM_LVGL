# 18 `core_export.list` 注意事项

原理、stub、tag 焊死见 [17_core_export与OpenCPU原理.md](17_core_export与OpenCPU原理.md)。  
本文只写**改这份名单之前要记住的事**。

文件：`components/apploader/src/core_export.list`  
当前版本：`@1.0`

```
改名单之前先过这十关

 1 名字必须在内核里        6 不要乱剥 #ifdef
 2 预处理之后还得在        7 导出了客户就会依赖
 3 底包和 SDK 成对重打     8 malloc 吃的是内核堆
 4 按改动升 MAJOR/MINOR    9 拒载时内核仍在、没有 UI
 5 只调行序不会对错        10 客户改 list 没有用
```

---

## 1. 名单里的名字必须是内核里已有的全局函数

`expgen --export` 要把每个名字解析成内核里的真地址。写了不存在的符号，底包链不过，或开机 `bsearch` 失败整份拒载。

拼写必须和实现**完全一致**。名单里已有 `ql_ssl_handshark` 这种历史拼写，不要擅自改成 `handshake`，除非内核符号也一起改。

```
core_export.list                 内核镜像
  ql_lcd_write        ──有──►   T ql_lcd_write     ✓ 能焊
  ql_foo_bar          ──无──►   （没有这个符号）    ✗ 底包链失败
  ql_ssl_handshake    ──无──►   T ql_ssl_handshark  ✗ 名字对不上

正确：
  内核先有全局 C 函数 ──► 再写进 list ──► 再 expgen
```

---

## 2. 客户想调的，预处理之后还得留在名单上

这份文件先当 C 做 `gcc -E`。只在头文件里声明、不进 list，客户链接报 `undefined reference`。只加 list、内核没有实现，底包链失败。

```
头文件有 ql_xxx          list 里有 ql_xxx         预处理后还在
    │                         │                        │
    ▼                         ▼                        ▼
  只声明、没桩            有桩、内核没实现           两边都有
  客户：链不过            方案：底包链不过           客户能编、开机能焊
```

正确顺序：

```
内核实现 ──► 写入 list（放对 #ifdef）──► 重编底包 ──► 重打 SDK ──► 客户再编 APP
```

---

## 3. 改名单必须重编底包，并重打客户 SDK

`core_stub.o` 和 `core_export.o` 是同一次 `expgen` 成对生成的。客户包里的 stub 必须和机器上的底包同一版。

只换应用、不换底包：新 API 的 MINOR 更大，或 tag 在旧表里不存在 → 拒载。

```
内部一次 expgen
        │
        ├──── core_export.o ──► 链进底包 ──► 方案烧整机
        │
        └──── core_stub.o  ──► pack_opencpu_sdk.sh
                                    │
                                    ▼
                             客户 ./build_app.sh
                             链进 C51_APP.pac

机器上：     底包 export          客户 APP stub
              必须是同一对 ────────┘
```

---

## 4. 版本怎么加（`@主.次`）

加载器比较 APPIMG 头里的 `stub_version` 和内核 `gCoreExportVersion`：

```
APPIMG @x.y                      内核 @a.b
        │                              │
        └──────── 比较 ────────────────┘

  主版本不同              应用次版本更大              主版本同且应用次版本更小或相等
 ┌────────────┐          ┌─────────────────┐        ┌────────────────────────┐
 │ 拒载       │          │ 拒载             │        │ 通过                   │
 │ 删/改语义  │          │ 新 SDK 配旧底包  │        │ 旧 APP 跑在只追加过    │
 │ 必须重发APP│          │                  │        │ 符号的新底包上         │
 └────────────┘          └─────────────────┘        └────────────────────────┘
```

| 改动 | `@` 怎么改 | 旧 APPIMG |
|------|------------|-----------|
| 只**追加**符号，旧符号语义不变 | 加 MINOR，例如 `@1.1` | 仍可加载 |
| **删除**、改参数、改语义、改名字 | 加 MAJOR，例如 `@2.0` | **全部拒绝** |
| 新 SDK（MINOR 更大）去刷旧底包 | —— | 加载失败 |

当前是 `@1.0`。没有把握不要动这行。

```
新 SDK @1.1 多了 ql_foo     旧底包 @1.0
        └──── 应用 MINOR 更大 ────┘  → 拒载

旧 APP @1.0                 新底包 @1.1（只追加）
        └──── 应用 MINOR 更小 ────┘  → 可通过

删了 ql_bar 还自称 @1.0     旧 APP 仍调 ql_bar
        └──── bsearch 找不到 ────┘  → 拒载
```

---

## 5. 只调换行序，不会把函数对错

加载按名字算出的 32 位 tag 做 `bsearch`，不是第 N 个槽。  
`ql_lcd_write` 的 tag 永远是 `0x43621e41`，写在第 3 行还是第 300 行都一样。

只重排、符号集合不变：旧 APPIMG 仍可跑在新底包上，也不必改 `@1.0`。  
真正危险的是重排时顺手删掉、改名、或改了 `#ifdef`。不要写两个同名。

```
名单原文（人看的行序）              内核表（按 tag 排序）

  第 1 行 malloc                    [k] tag=43621e41  ql_lcd_write
  第 2 行 ql_lcd_write              [n] tag=e3c52ebf  malloc
  … 调成 …
  第 1 行 ql_lcd_write
  第 2 行 malloc

  行号变了，每个名字的 tag 不变 ──► bsearch 仍能找到
```

对比：如果它是「第 N 槽」，重排就会对错（**实际不是这样**）：

```
  错觉（按槽）                         实际（按 tag）
  名单第 2 行 = 槽[2]                  stub.tag ──bsearch──► 同名函数
  重排后槽[2] 变成别人  ✗ 会跳飞        重排后 tag 仍指向原函数  ✓
```

---

## 6. `#ifdef` 跟着产品 feature，不要乱剥

`#ifndef CONFIG_QUEC_PROJECT_FEATURE` 挡住的 `vfs_*` / `drv*` 是有意的：客户应走 `ql_fopen` / `ql_lcd_*`。  
剥掉这些宏，等于把芯片原厂接口交给终端，底包一换实现就可能变。

```
list 原文
        │ gcc -E
        ▼
┌────────────────────────────────────────────┐
│ 无条件：osi* malloc lwip_* mbedtls_* 留下  │
│                                            │
│ #ifndef QUEC_PROJECT   本项目已定义 → 丢掉 │
│   vfs_*  drvLcd*  drvUart*  auPlayer*      │
│                                            │
│ #ifdef OPEN_EXPORT     本项目已开 → 留下   │
│   ql_*  再按 FEATURE_HTTP / MQTT … 裁      │
└────────────────────────────────────────────┘

乱剥 #ifndef QUEC_PROJECT：
  客户能调 drvLcdBlockTransfer
  下次换底包驱实现一变 ──► 客户工程大面积要改
```

---

## 7. 导出 = 客户会永久依赖

一旦进了正式 SDK，删符号就要升 MAJOR，所有旧 APP 拒载。内部调试函数不要写进 list。

```
正式发出去的 SDK
        │
        ▼
  客户代码开始写 ql_xxx()
        │
        │  你后来想删 ql_xxx
        ▼
  必须 @2.0，旧 C51_APP.pac 全部不能加载
  等于逼客户重编重发

没发出去的内部函数 ──► 不要进 list
```

---

## 8. `malloc` / `free` 在名单上：不是进程隔离

客户 `malloc` 吃的是内核堆。128KB 只圈住 APPIMG 的 `.data/.bss/.corestub`。野指针可以打到内核。详见 [13_内存划分说明.md](13_内存划分说明.md)。

```
客户 APPIMG 128KB                    内核 AP RAM
0x80FA0000                           0x80C00000
┌─────────────────┐                  ┌──────────────────┐
│ .corestub       │                  │ 内核 .data/.bss  │
│ .data / .bss    │   malloc()       │                  │
│                 │ ───────────────► │ 内核堆           │
└─────────────────┘   stub 焊到      │ 任务栈 / LVGL 缓冲│
                      内核 free      └──────────────────┘

  圈住的只是左边这 128KB
  堆、任务栈、全屏缓冲都在右边
```

---

## 9. 加载失败时内核仍会起来，只是没有 UI

魔数、CRC、签名、入口越界、段落打到内核/BT 窗口、stub 版本不对、`bsearch` 对不上 tag，`appImageFromMem` 都返回 false，**不调** `appimg_enter`。查 log tag `APPL`。

```
开机
  │
  ▼
内核 APP 起来  ──►  FS / 网络 / 底包正常
  │
  ▼
appImageFromMem(0x60250000)
  │
  ├─ 校验通过 ──► enter() ──► 手表 UI
  │
  └─ 任一失败 ──► 不调 enter
                  机器能起，黑屏 / 没有表盘
                  不是整机砖了
```

---

## 10. 客户工程里改这份 list 没有意义

打包时 list 不会作为客户可改输入。客户只有冻住的 `sdk_prebuilt/core_stub.o`。  
要加 API：方案改 list → 重编底包 → `./pack_opencpu_sdk.sh` 再发。

```
方案仓库                            客户 SDK
core_export.list                    （没有这份 list，改了也不跑 expgen）
        │
        ▼
   expgen → stub + export           sdk_prebuilt/core_stub.o   ← 冻住
        │                                      │
        ▼                                      ▼
   底包烧到机器                      客户只能链这一份

客户本地改 list：
  不会重新生成 stub
  机器上的 export 也不会变
  等于没改
```

---

## 对照：改动类型和后果

```
只调行序          符号集合不变          不必改 @     旧 APP 仍可用
只追加符号        旧语义不变            @1.0 → @1.1  旧 APP 仍可用
删 / 改名 / 改参  ABI 断了              @1.0 → @2.0  旧 APP 全部拒载
只改客户工程 list 客户侧无效            ——           无
```

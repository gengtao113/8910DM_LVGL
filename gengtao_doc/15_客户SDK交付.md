# 15 客户 SDK 交付（只编 APPIMG）

方案内部继续编完整底包。发给终端客户的是 **另一份树**：只能编 / 替换 APPIMG，**不含** `kernel/`、`hal/`、`driver/` 的 `.c` 实现。

打包入口：仓库根目录 `./pack_opencpu_sdk.sh`（须先成功编过一次完整固件）。  
客户入口：解开后 `./build_app.sh`。

---

## 1. 为什么不能把整仓交给客户

当前仓库是 **CCSDK + 完整内核源码** 叠在一起：

| 目录 | 给客户？ |
|------|----------|
| `ql-application/` | 要，客户改这里 |
| `ql-kernel/inc/` | 要，`ql_*` 头文件 |
| `ql-kernel/libs/*.a` | 不要（链进内核，不是 APPIMG；且是 API 实现） |
| `kernel/src` `hal/src` `driver/src` | **不要** |
| `kernel/include` `hal/include` `driver/include` | 要（头文件）。`ql_log.h` → `osi_log.h`，触摸 → `hal_chip.h` |
| 预编译底包 `C51.pac` / merge | **不要**（整机由方案烧） |
| `core_stub.o` | 要，且必须和方案底包同一版 |

整仓交付 = 内核源码也交出去。客户编 APPIMG **并不需要**那些 `.c`。

```
内部（本仓库）                         客户 SDK
┌─────────────────────────┐           ┌──────────────────────────┐
│ kernel/hal/driver 源码  │  不拷     │ （无 src）               │
│ ql-kernel/libs/*.a      │  不拷     │                          │
│ 编出底包 + core_stub.o  │─────────►│ sdk_prebuilt/            │
│ ql-application          │  拷       │ ql-application           │
│ ql-kernel/inc + 公开头  │  拷       │ inc / kernel/include …  │
│ prebuilts + tools       │  拷       │ 只编 APPIMG 的工具链     │
└─────────────────────────┘           └──────────────────────────┘
```

---

## 2. 客户侧只做什么

```
./build_app.sh
  cmake 只编 ql-application + 预置 core_stub.o
  → C51_APP.img / C51_APP.pac
  → QFlash 只下 APPIMG 分区
```

客户**不应出现整机包**。底包由方案先烧好；客户开发完只替换自己的应用分区。

客户 **不再** `add_subdirectory(kernel/hal/driver)`。  
CMake 里用三个空的 `INTERFACE` 库顶掉现有的 `target_link_libraries(... kernel driver)` / `target_include_targets(hal)`（`ctp`、`function` 还在链这些名字，只为拿头文件）。

`ql_*` 仍然走 stub → 底包里的实现，和整仓编出来的 APPIMG 是同一条 ABI。底包或 `core_export.list` 一变，必须 **重打 SDK**，不能只换应用工程。

---

## 3. 打包前必须有一份完整编译

`core_stub.o`、生成头、`partinfo.cmake`、`target.cmake`、FDL 都来自内部一次 `./build_all.sh`。整机 `C51.pac` 不打进客户包。

```bash
./build_all.sh new C56U_HM_SH8     # 内部
./pack_opencpu_sdk.sh                      # 默认写出 OpenCPU_SDK_Output/OpenCPU_SDK_C51/
./pack_opencpu_sdk.sh /tmp/sdk_out         # 指定目录
```

缺 `out/8915DM_cat1_open_release/lib/core_stub.o` 或 `target/C51/*.pac` 会直接失败。

---

## 4. 客户包里有什么 / 没有什么

有：

```
OpenCPU_SDK_C51/
  build_app.sh
  CMakeLists.txt                 # 只编 APPIMG
  cmake/  tools/  prebuilts/linux/
  components/ql-application/
  components/ql-kernel/inc/
  components/kernel/include/     # 仅头文件
  components/hal/include/
  components/driver/include/
  components/newlib/armca5/      # libc/libm
  components/apploader/pack/app_flashimg.ld
  components/ql-config/build/EC600UCN_LB/8915DM_cat1_open/
  projects/C56U_HM_SH8/
  sdk_prebuilt/
    core_stub.o
    target.cmake  partinfo.cmake
    include/                     # 生成的 CONFIG / hal_config.h
    fdl1.sign.img  fdl2.sign.img # 只为打 APP.pac，不是整机固件
```

没有：`kernel/**/*.c`、`hal/src`、`driver/src`、`bootloader`、`modem`、`ats`、`cfw` 源码、`ql-kernel/libs/*.a`。

---

## 5. 版本必须锁死

客户 SDK 和底包是一对：

| 变了 | 客户侧 |
|------|--------|
| 只改 `ql-application/` | 重编 APPIMG，烧 `C51_APP.pac` 到 APPIMG 分区 |
| 改 `core_export.list` / 分区 / VoLTE / 内核 | 内部重编底包，**重新 pack_opencpu_sdk.sh** 再发给客户 |

stub 主版本对不上，加载器会拒绝 `appimg_enter`。

---

## 6. 还没自动处理的

- Windows 客户包（可再拷 `prebuilts/win32` + bat）
- 应用里误 `#include` 了未放入公开头的内核私有头，客户编会失败，应改成 `ql_*` 或把该头加入打包白名单
- `lv_port/` 引用 `drv_lcd_v2.h`，当前 CMake 不编进固件，不要在客户工程打开

内部日常仍用 `./build_all.sh`。`pack_opencpu_sdk.sh` 只在给客户发版时跑。

# 16 客户 SDK 变更说明（对照当前 git status）

本批改动做两件事：

1. **仓库里不再跟踪固件产物** `target/C51`（每次编译都会变）。
2. **内部可以打一份只含 APPIMG 的客户 SDK**，客户只编应用、只烧自己的分区，不出现整机包。

交付设计见 [15_客户SDK交付.md](15_客户SDK交付.md)。本文只对照工作区里**要提交 / 不要提交**的文件。自己提交，不要把 `commit.txt` 或打包产物加进去。

---

## 1. 已经暂存：取消跟踪 `target/`

`git rm -r --cached target/` 已经做完。磁盘上的 `target/C51` 还在，只是不再进库。

状态里那一长串「删除」是**从 git 索引里拿掉**，不是删本机烧录文件：

```
target/C51/8915DM_cat1_open.elf
target/C51/8915DM_cat1_open_C51.pac
target/C51/8915DM_cat1_open_C51_merge.pac
target/C51/app/C51_APP.*
target/C51/prepack/*
```

`.gitignore` 已有 `/target/`。已跟踪文件不受 ignore 影响，所以必须先 `--cached` 一次。提交后同事拉代码不会丢本机产物（他们本地若编过仍在）；仓库里不再带着几 MB 的 pac/elf。

---

## 2. 尚未暂存：建议纳入

| 文件 | 作用 |
|------|------|
| `.gitignore` | `/target/`、`/OpenCPU_SDK_Output/`、`/commit.txt`、`/tools/linux/lib/libicu*.so.55*` |
| `pack_opencpu_sdk.sh` | 内部打包入口。默认写出 `OpenCPU_SDK_Output/OpenCPU_SDK_C51/` |
| `sdk/build_app.sh` | 打进客户树的编译入口。只出 `C51_APP.pac`，不做 `pacmerge` |
| `cmake/sdk_app_only.cmake` | 客户树的 `CMakeLists.txt`。不编 kernel/hal/driver 源码，用 INTERFACE 顶掉链接名 |
| `components/ql-application/init/CMakeLists.txt` | `QL_SDK_APP_ONLY` 时在同一目录 `sign_image`，否则 ninja 没有 `C51_APP.sign.img` 规则 |
| `gengtao_doc/12_客户开发说明.md` | 指向 15 与 `pack_opencpu_sdk.sh` |
| `gengtao_doc/14_OpenCPU开发说明.md` | 发给客户应打 APPIMG-only SDK |
| `gengtao_doc/15_客户SDK交付.md` | 交付内容、客户只烧 APPIMG、不提供 merge |
| `gengtao_doc/16_客户SDK变更说明.md` | 本文 |
| `gengtao_doc/README.md` | 目录加上 15 / 16 |

建议提交命令：

```bash
git add \
    .gitignore \
    pack_opencpu_sdk.sh \
    sdk/build_app.sh \
    cmake/sdk_app_only.cmake \
    components/ql-application/init/CMakeLists.txt \
    gengtao_doc/12_客户开发说明.md \
    gengtao_doc/14_OpenCPU开发说明.md \
    gengtao_doc/15_客户SDK交付.md \
    gengtao_doc/16_客户SDK变更说明.md \
    gengtao_doc/README.md

# target/ 的删除已在暂存区，不要 git restore --staged target/
```

建议说明（`-m` 或另写）：

```
交付 OpenCPU 客户 SDK：只编 APPIMG，仓库不再跟踪 target 产物。

内部 ./pack_opencpu_sdk.sh 打出不含 kernel/hal/driver 源码的树；
客户 ./build_app.sh 只生成 C51_APP.pac，整机底包由方案烧。
```

---

## 3. 不要提交

| 路径 | 原因 |
|------|------|
| `OpenCPU_SDK_Output/` | 打包产物，已 ignore |
| `/out/` | 中间编译目录 |
| `/target/` | 固件产物（索引删除即可，不要再 `git add target`） |
| `/commit.txt` | 本地提交备忘，已 ignore |
| `prebuilts/linux/**`、`tools/linux/**` | 本机 chmod / 符号链接噪音；除非单独修 `nanopb/protoc` 的 CRLF |

`sdk/` 目录里目前只有 `build_app.sh`，加这一份即可。

---

## 4. 脚本怎么用（提交之后）

内部（完整树，先有一次 `./build_all.sh`）：

```bash
./pack_opencpu_sdk.sh
# → OpenCPU_SDK_Output/OpenCPU_SDK_C51/
```

客户（解开后的那份树）：

```bash
./build_app.sh
# → target/C51/app/C51_APP.pac
# QFlash 只下 APPIMG，不要 merge
```

客户包里没有 `8915DM_cat1_open_C51.pac` / `*_merge.pac`。新机首烧仍由方案用内部底包处理。

---

## 5. 和 Linux 编译那批的关系

`commit.txt` 里是 `build_all.sh`、queue.c、osi_blue_screen、CTP 头文件大小写、`nanopb/protoc` CRLF 等，**另一批**。可以分两次提交：

1. Linux 能编完整底包（按 `commit.txt`）
2. 客户 SDK 打包与取消跟踪 `target/`（按本文）

不要把 `OpenCPU_SDK_Output` 或 `target/C51` 产物加进任何一次提交。

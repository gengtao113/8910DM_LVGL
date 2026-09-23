# 07 SPI NOR Flash：驱动、HAL 与型号表

本文把 `drv_spi_flash.c`、`hal_spi_flash.c`、`hal_spi_flash_prop.csv` 串成一篇，说明 8910 上主/外挂 NOR 如何识别、读写擦、做 XIP 保护和写保护。

> 相关源码  
> - 驱动：`components/driver/src/spi_flash/drv_spi_flash.c`  
> - 驱动头：`components/driver/include/drv_spi_flash.h`  
> - HAL：`components/hal/src/hal_spi_flash.c`  
> - HAL 头：`components/hal/include/hal_spi_flash.h`  
> - 命令/寄存器：`components/hal/src/hal_spi_flash_internal.h`、`include/hal_spi_flash_defs.h`  
> - 型号表：`components/hal/src/hal_spi_flash_prop.csv`  
> - 表生成：`tools/norpropgen.py`

---

## 1. 分层

```
应用 / 文件系统
        ↓
drv_spi_flash.c     实例、XIP、suspend 循环、软写保护、按 page/4K 切块
        ↓
hal_spi_flash.c     按 JEDEC ID 查表、发 opcode、volatile 硬件 WP
        ↓
SPI Flash 控制器    hwp_spiFlash / hwp_spiFlash1
        ↓
NOR 芯片
```

| 层 | 负责 | 不负责 |
|----|------|--------|
| drv | 互斥、关中断、有 IRQ 就 suspend、64KB 软保护、AHB/I-Cache、按页/扇区循环 | 具体 opcode、型号差异 |
| HAL | `0x9F` 认片、SR/WP/QE、`02H/20H/52H/D8H/75H/7AH` | 等擦完、插中断（`WaitWipFinish` 只给不许插队的路径） |
| CSV | 这颗片能不能 suspend、怎么写 SR、有没有 OTP | 命令编码（写死在 HAL） |

热路径分别链到 `.ramtext.flashdrv` / `.ramtext.flashhal`。代码跑在同一颗 NOR 上时，擦写自己必须先把这些函数放到 RAM。

---

## 2. 驱动层（drv_spi_flash.c）

### 2.1 实例（8910）

每个 name 单例，`drvSpiFlashOpen()` 只初始化一次，没有 close。

| name | 控制器 | 映射基址 | XIP |
|------|--------|----------|-----|
| `DRV_NAME_SPI_FLASH` | `hwp_spiFlash` | `CONFIG_NOR_PHY_ADDRESS` | 强制开，不能关 |
| `DRV_NAME_SPI_FLASH_EXT` | `hwp_spiFlash1` | `CONFIG_NOR_EXT_PHY_ADDRESS` | 默认开，可 `SetXipEnabled(false)` 换速度 |

上下文要点：`opened`、`xip_protect`、`suspend_disable`、`osiMutex`、`block_prohibit[8]`（64KB 位图，最多罩 16MB）。

### 2.2 读：当内存用

不发 SPI 读命令，把 `base_address + offset` 当 AHB 映射再 `memcpy` / `memcmp`。

- `drvSpiFlashRead` / `ReadCheck` / `MapAddress`
- 擦写结束后 `osiDCacheInvalidate`，避免 CPU 看到旧 cache

`MapAddress` 只宜只读。和外挂、非控制器 Flash 兼容时优先走 `Read`。

### 2.3 写 / 擦：两条路径

写之前**不会自动擦**，调用方先 Erase 再 Write。不对齐 page 的写拆到页边界（256B）。擦要求 offset/size 都 4K 对齐，并尽量挑大块：64K → 32K → 4K。

| | XIP 开（默认，主片） | XIP 关（外挂文件系统常用） |
|--|--|--|
| 并发 | mutex + 关中断；有 IRQ 则 suspend | 只拿 mutex，HAL 死等 WIP |
| 实现 | `prvPageProgram` / `prvErase` | `prvPageProgramNoXipLocked` / `prvEraseNoXipLocked` |

保护两层：mutex 保证写互斥（suspend 会切线程）；`xip_protect` 时再进临界区。

### 2.4 XIP 擦写循环（主片）

`prvPageProgram` / `prvErase`：

1. `osiEnterCritical`，关 AHB 读（8910 实际是 `L1C_InvalidateICacheAll()`）
2. `Prepare` → PROGRAM/ERASE → **至少等 100µs**（刚 resume 又 suspend 会把擦写拖死）
3. 轮询 WIP
4. 若 `suspend_en && !suspend_disable && osiIrqPending() && !panic`：  
   `75H` SUSPEND → 等 20µs → 开 AHB → 出临界区让中断走 → 再进临界区 `7AH` RESUME → 再等 100µs
5. 完成后 `Finish`，开 AHB，出临界区

关中断或 ISR 里调擦写时必须先 `drvSpiFlashSetSuspendEnabled(false)`，否则可能永远擦不完。

### 2.5 软写保护（和硬件 WP 不是一回事）

按 64KB block 置位。`start` 向上对齐、`end` 向下对齐，对不齐 64K 等于没设上。擦写命中保护区返回 false。典型用法：锁住 bootloader 所在块。

```c
drvSpiFlashSetRangeWriteProhibit(flash, 0, 0x10000);  // 锁第一块 64KB
```

### 2.6 其它 API

状态寄存器、Deep Power Down、Unique ID / CPID、Security Register（OTP：读按 4B，写按 128B，**无 suspend**）、SFDP。这些走 `prvProtectAquire`：mutex +（可选）关中断。

`CONFIG_QUEC_PROJECT_FEATURE` 下，擦完按 4K 累计 `quec_erase_cnt[]` / `quec_erase_total`，只统计 `CONFIG_FS_SYS_FLASH_*` 那段，用来看磨损。

### 2.7 使用注意

1. Write 只 PROGRAM，先 Erase。
2. `data` 不能落在正在操作的那片 Flash 上（关 AHB 后会读飞）。
3. 主控制器那路不能关 XIP。
4. 没有回读校验，要验自己 `ReadCheck`。
5. `ChipErase` 只给产线/调试。

---

## 3. HAL 层（hal_spi_flash.c）

`halSpiFlash_t` 嵌在驱动上下文里：`hwp` 由驱动静态表填，其余由 `halSpiFlashInit()` 按 ID 填。手册和实现不一致时，可用 `drvSpiFlashGetProp()` 改字段（要清楚后果）。

### 3.1 初始化

```
halSpiFlashInit()
  1. 0x9F RDID → mid
  2. prvFlashPropsByMid() 查 CSV 生成的表
  3. halSpiFlashStatusCheck() 按厂商修 SR
```

容量运行时强制为 `1 << mid[23:16]`，不信表里的 `capacity` 列。认不出 mid 直接 `osiPanic()`。

`StatusCheck` 要保证：

| 目标 | 做法 |
|------|------|
| 上电残留 WIP/WEL/SUS | 有则 `66H+99H` 软复位 |
| Quad XIP 能读 | 置 `QE` |
| 平时不能乱写 | 支持 volatile WP 时，BP 设成「保护全部」 |

GD/Winbond/Puya/XTX/XMCC 走 `prvStatusCheckGD`；XMCA 还要进 OTP 设 TB；XMCB 只保证 QE。

### 3.2 擦写前后：volatile 硬件 WP

支持 `volatile_sr_en` 时：

- **Prepare**：按 offset 查 WP 表，把 SR 改成「从该地址往上尽量少保护」，再发 **`06H` WREN**。
- **Finish**：再写回「保护全部」。用 **`50H`** 写 volatile SR，掉电不把 OTP 式 BP 写死。

不支持 volatile SR 时这两步只发 WREN。WP 不能任意切，只能选表档（全保护、后 1/2、后 32K…）。`halSpiFlashWpRange()` 返回实际罩住的区间。

GD 系按 4K 扇区数查 `gGD8M/16M/32M/64M/128MWpMap`；XMCA 按容量的 1/128 查 `gXmcaWpMap`。

这和驱动 64KB **软件** `block_prohibit` 是两套：软保护拦应用调错地址，硬保护防跑飞误写。

### 3.3 标准 NOR 命令

| 操作 | 命令 | 说明 |
|------|------|------|
| Page Program | `02H` + 24bit 地址 | 数据不能来自正在操作的那片；长度受 TX FIFO 限，驱动按 256B 切 |
| Erase | `20H` 4K / `52H` 32K / `D8H` 64K | 驱动按对齐选 |
| Chip Erase | `C7H` | |
| Suspend / Resume | `75H` / `7AH` | program/erase 共用 |
| WIP | `05H` SR1 bit0 | 隔 1µs 连读两次为 0 才算完 |
| Deep PD | `B9H` / `ABH` | 唤醒后再等 50µs |
| WREN / WRDI | `06H` / `04H` | |
| RDSR / WRSR | `05H` / `35H` / `01H` / `31H` | 是否双 SR、一次写 16bit 看表 |
| RDID / SFDP | `9FH` / `5AH` | |

HAL 的 `PageProgram`/`Erase` **只发命令就返回**。等完成、有中断就挂起，都在 drv。`WaitWipFinish` 是死等，给 ChipErase、写 SR、Security Register 用。不要在 HAL 里加「等擦完」，会破坏 suspend。

### 3.4 控制器发命令

`halSpiFlashCmd()` + `hal_spi_flash_internal.h`：

1. 等控制器 `busy` 清掉（读两次防毛刺）
2. 清 TX/RX FIFO
3. 设 RX 长度、FIFO 位宽
4. 字节推进 FIFO
5. 写 `spi_cmd_addr` 触发

命令字：

- `CMD_ADDRESS(op, addr)`：opcode 在低 8 位，地址左移 8（PP/Erase）
- `EXTCMD_NORX(op)`：无回读
- `EXTCMD_SRX(op)`：单线回读

Security Register / UID 按 `type` 分叉（GD 一套，XTX、XMCB 另走）。驱动按 4B 读、128B 写，是因为 HAL 限制读 ≤4、写不能填满 TX FIFO。

---

## 4. 型号表（hal_spi_flash_prop.csv）

### 4.1 怎样进固件

```
hal_spi_flash_prop.csv
        ↓  python3 tools/norpropgen.py
hal_spi_flash_prop.h     // { .mid=..., .type=..., },
        ↓  #include
gSpiFlashProps[]
```

第一列型号名只进注释。其余列名必须和 `halSpiFlash_t` 字段一致。改表后要重编 `components/hal`。

### 4.2 mid 编码

```
mid[7:0]   厂商      C8=GD  EF=Winbond  85=Puya  0B=XTX  20=XMC
mid[15:8]  存储类型
mid[23:16] 容量指数  运行时 capacity = 1 << 这一段
```

例：`GD25LE64E = 0x1760c8` → 厂商 C8，类型 60，指数 0x17 → **8MB**。

### 4.3 三级匹配（表从上到下）

| 优先级 | 比较 | 谁会中 |
|--------|------|--------|
| 1 | 整 mid | 具体型号行 |
| 2 | `mid & 0xffff` | 同系列不同容量 |
| 3 | `mid & 0xff` | 最后两行 `GD=0xC8`、`WINBOND=0xEF` |
| 都没有 | `osiPanic()` | 新片子没入表 |

**具体型号必须写在厂商兜底前面。** 新 GD 片行为不同就要单独加行。

### 4.4 各列含义

| 列 | 决定什么 |
|----|----------|
| `type` | Security 命令、StatusCheck 走 GD/XMCA/XMCB |
| `wp_type` | Prepare/Finish 用哪张 BP 表；`0` 表示不改硬件 WP |
| `volatile_sr_en` | 1：擦写前后 `50H` 改 SR；0：只 WREN |
| `suspend_en` | 1：驱动才发 `75H/7AH`；0：擦写一直关中断 |
| `has_sr2` / `write_sr12` | 是否再读 `35H`；`01H` 一次写还是 `01H+31H` |
| `has_sus1/sus2` | 上电 SUS 位置位则软复位 |
| `uid_type` / `cpid_type` | UID 走 `4BH` 还是 SFDP 偏移 |
| `sreg_*` | OTP 块大小和编号；`max=0` 等于没有 |
| `sfdp_en` | 能否发 `5AH` |

### 4.5 当前表内型号

| 型号 | mid | 容量 | suspend | volatile WP | 备注 |
|------|-----|------|---------|-------------|------|
| XT25W32B / 64B | `16600b` / `17600b` | 4/8MB | 无 | 有（GD 风格） | 主片擦写会长时间关中断 |
| XM25QU64A | `173820` | 8MB | 无 | 有（XMCA 表） | StatusCheck 进 OTP 设 TB |
| XM25QU64B | `175020` | 8MB | 有 | 无 | 只保证 QE |
| XM25QU16C / 32C | `155020` / `165020` | 2/4MB | 无 | 有 | XMCC 命令，GD 风格 WP |
| GD25LE64E | `1760c8` | 8MB | 有 | 有 | 典型主片 |
| GD25LQ128C | `1860c8` | 16MB | 有 | 有 | |
| GD25Q127C | `1840c8` | 16MB | 有 | **无** | 不解硬件 BP，靠驱动软保护 |
| W25Q64JV | `1740ef` | 8MB | 有 | 有 | 无 SUS2 |
| P25Q32L / 64L | `166085` / `176085` | 4/8MB | 有 | 有 | |
| P25Q128L | `186085` | 16MB | **无** | 有 | |
| GD / WINBOND | `C8` / `EF` | 兜底 | 有 | 有 | 未点名的同厂片子 |

`sreg` 为 `1..3` 的（多数 GD/Winbond/Puya）才有 3 块 OTP；XTX、XMCA 的 `0,0` 表示 Security API 会失败。

---

## 5. 一次完整写（主片 XIP + 典型 GD）

以表认到 `GD25LE64E`、`suspend_en=1`、`volatile_sr_en=1` 为例：

```
drv 锁 mutex、关中断、关 AHB / 刷 I-Cache
  HAL Prepare：volatile SR 按 offset 降低 BP → 06H
  HAL 02H 写一页（或 20H/52H/D8H 擦）
  drv 至少等 100µs，再轮询 05H WIP
      有 IRQ：75H → 开 AHB → 出临界区
      中断处理完：关 AHB → 7AH → 再等 100µs
  HAL Finish：volatile SR 锁全部
drv 开 AHB、出临界区、DCache invalidate
```

换成 `XT25W32B`：循环永远等 WIP，主片擦 64K 时中断会饿很久。  
换成 `GD25Q127C`：Prepare 不解 BP；出厂若锁着文件系统，擦会失败或空操作。

---

## 6. 加新片子 / 排障

### 6.1 加料步骤

1. `0x9F` 读 mid，写成 `容量指数<<16 | 类型<<8 | 厂商`。
2. 对照手册填 suspend / volatile SR / SR2 / UID / OTP。
3. **插在 GD、WINBOND 两行之前。**
4. 重编 `components/hal`。
5. 拿不准时宁可 `suspend_en=0` 再实测 WP，不要抄错行（「能 suspend 其实不能」会卡死或损坏）。

CSV 不改 opcode，只改**这颗片能不能用、用哪套 SR/WP/UID**。主片换料，第一件事对表：有没有它、`suspend_en` 和 `volatile_sr_en` 对不对。

### 6.2 常见现象

| 现象 | 先看 |
|------|------|
| 启动直接 panic | mid 不在表里，或只命中兜底但字段拷贝异常 |
| 擦不进去 | StatusCheck 把 BP 锁死；`volatile_sr_en=0` 解不开；驱动软保护命中 |
| 擦写时系统卡死、中断饿死 | 表里 `suspend_en=0`，或关中断里擦写却没关 suspend |
| 擦写永远不结束 | 关中断路径仍开着 suspend |
| Quad 读花、跑飞 | `QE` 没置上 |
| OTP/UID 失败 | `sreg_*` / `uid_type` 与手册不符 |
| 写完读到旧数据 | 忘了 DCache invalidate，或 data 指针在同一片 Flash 上 |

---

## 7. 一句话

`drv_spi_flash.c` 是 **XIP 管家**（关取指、suspend 让中断、软保护、按页/扇区循环）。  
`hal_spi_flash.c` 是 **按 JEDEC ID 查表的命令集**，并用 volatile SR 做临时硬件写保护。  
`hal_spi_flash_prop.csv` 是 **型号能力数据库**：suspend、WP、SR、UID/OTP 都由它决定。  
三层叠在一起，才能在「代码跑在同一颗 NOR 上」时既擦得动，又能响应中断。

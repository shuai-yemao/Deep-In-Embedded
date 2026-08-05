---
title: FATFS 嵌入式移植指南
date: 2026-08-05
tags:
  - 嵌入式
  - 中间件
  - FATFS
  - STM32
  - FreeRTOS
  - Flash
  - W25Q64
  - FAL
  - SFUD
aliases:
  - FATFS 嵌入式移植
---

# FATFS 嵌入式移植指南

> [!summary] 这篇指南解决什么问题
> 把 FatFs R0.15 通过 FAL → SFUD → SPI 链路移植到 STM32F411CEU6 + FreeRTOS 工程，接入外部 W25Q64 SPI Flash 的 1MB `fatfs` 分区，配置 4KB 逻辑扇区（对齐擦除粒度）自动形成 FAT12，并说明源码来源、分层架构、ffconf.h 裁剪、diskio 移植、构建集成和验证方法。

## 0. 先看结论

本工程已经完成以下集成：

- FatFs R0.15 官方源码已放入 Middlewares/FATFS（ff.c / ff.h / ffconf.h / ffunicode.c / diskio.h）。
- 移植层 Middlewares/FATFS/port/diskio.c 以 **FAL diskio 模式**桥接 `fatfs` 分区，形成 FatFs → FAL → SFUD → SPI → W25Q64 完整链路。
- 逻辑扇区固定 4KB（`FF_MIN_SS == FF_MAX_SS == 4096`），一一对应 W25Q64 的 4KB 擦除粒度，disk_write 逐扇区「先擦后写」。
- FAL 分区表新增 `fatfs` 分区：偏移 5MB、大小 1MB（`Middlewares/FAL/port/fal_cfg.h`）。
- 1MB 分区 ÷ 4KB 扇区 = 256 簇，自动选择 **FAT12**（簇数远小于 FAT16/FAT32 阈值）。
- 裁剪 `FF_FS_TINY=1`、`FF_USE_LFN=2`、`FF_CODE_PAGE=437`、`FF_FS_NORTC=1`、`FF_FS_REENTRANT=0`。
- CMake 构建通过；RTT 实测 `f_mount → f_mkfs → 写 test.txt → 读回校验 → f_getfree` 通过。
- 掉电持久化（断电重启后文件仍在）尚未单独验证，测试线程每次上电会先擦除重测。

" 文件系统跑通 " 不等于 " 业务文件设计已完成 "。当前只是跑通了挂载 / 格式化 / 读写闭环。

想了解移植完成后怎么读写文件，请看 [[FATFS嵌入式使用手册]]。

## 1. FATFS 是什么，为什么分层

FATFS 是一个跑在裸介质上的 FAT 文件系统，但它不直接操作 Flash——它通过磁盘接口 `disk_xxx()` 访问介质。这样同一套代码可以跑在不同存储介质上（SD 卡、Flash、RAM 盘）。

本工程的完整调用链：

~~~text
应用层     user_fatfs.c              f_mount / f_mkfs / f_open / f_read / f_write / f_getfree
             │  （全部在 fatfs_test 线程内，无自身锁）
             ▼
FatFs     src/ff.c                   FAT12 卷管理、目录项、LFN 编码
             │  ff.h 依赖 diskio.h 声明的 disk_xxx 接口
             ▼
diskio   port/diskio.c               disk_initialize / disk_read / disk_write / disk_ioctl
             │  懒查询 fal_partition_find("fatfs") 缓存分区句柄
             ▼
FAL       src/fal_partition.c        fal_partition_read / write / erase（fatfs 分区）
             │  ops 回调指向 nor_flash0（W25Q64 整片）
             ▼
SFUD      src/sfud.c                 sfud_read / sfud_write / sfud_erase
             │  每次读写擦前自动 lock/unlock（锁 = s_sfud_shared_mutex）
             ▼
SFUD port drv_adapter_sfud_externflash.c  绑定 wr/lock/unlock，复用 Wrapper get_spi
             │
             ▼
硬件      W25Q64 SPI NOR Flash
~~~

每一层只做一件事：

| 层 | 职责 |
| -- | ---- |
| FatFs | 文件系统本体：卷管理、目录树、FAT 表、LFN 文件名 |
| diskio（本工程移植层） | 把 FatFs 的"逻辑扇区"翻译成 FAL 分区的"字节偏移"，逐扇区擦写 |
| FAL | Flash 抽象层：把"分区"概念提供给上层，屏蔽具体 Flash |
| SFUD | 串行 Flash 通用驱动：JEDEC 识别、页编程、扇区擦除 |
| FreeRTOS | 提供底层 SPI 互斥锁，串行化多任务对 SPI 的访问 |

关键设计：**线程安全不在 FatFs 层**。`FF_FS_REENTRANT=0`，FatFs 内部不加锁（因此本工程不需要 ffsystem.c），靠"单卷单任务访问 + SFUD 共享互斥"保证安全（见第 4 节 Step 5）。

## 2. 官方源码地址

FatFs 官方源码托管在 elm-chan 个人站点，GitHub 有镜像仓库。本工程锁定官方原版，不修改 FatFs 内部实现，只新增 port 层文件。

| 官方文件 | 作用 | 本工程位置 |
| ------- | ---- | ---------- |
| `ff.c` | FatFs 内核（约 7000 行） | Middlewares/FATFS/src/ff.c |
| `ff.h` | 公共 API 头（R0.15） | Middlewares/FATFS/inc/ff.h |
| `ffconf.h` | 板级配置模板 | Middlewares/FATFS/inc/ffconf.h（本工程裁剪点） |
| `ffunicode.c` | LFN 码表（`FF_USE_LFN>=1` 必需） | Middlewares/FATFS/src/ffunicode.c |
| `diskio.h` | 磁盘接口声明 | Middlewares/FATFS/inc/diskio.h |

官方链接：

- FatFs 官网：[elm-chan.org/fsw/ff](http://elm-chan.org/fsw/ff/00index_e.html)
- GitHub 镜像：[ChaN/ff](https://github.com/ChaN/ff)
- 本工程使用 R0.15（ff.h 头部 `FatFs - Generic FAT Filesystem module R0.15`，`FFCONF_DEF` = 80286）。

## 3. 本工程目录结构

~~~text
Middlewares/
├── FATFS/
│   ├── inc/                    # 官方头文件
│   │   ├── ff.h                # R0.15
│   │   ├── ffconf.h            # 板级配置（本工程裁剪点）
│   │   └── diskio.h            # 磁盘接口声明
│   ├── src/                    # 官方源码
│   │   ├── ff.c                # FatFs 内核
│   │   └── ffunicode.c         # LFN 码表
│   └── port/
│       └── diskio.c            # ★ 本工程移植层：桥接 FAL "fatfs" 分区
├── FAL/                        # Flash 抽象层（fatfs 分区定义在 port/fal_cfg.h）
├── SFUD/                        # 串行 Flash 驱动
└── ...
~~~

用户测试任务在应用层：

- `User_Task/User_Fatfs/Src/user_fatfs.c` —— 挂载 / 格式化 / 写读校验 / 容量查询测试线程。
- `User_Task/User_Fatfs/Inc/user_fatfs.h` —— `user_fatfs_init()` + 调试观测变量。

注意：工程里**没有 `ffsystem.c`**。官方它提供互斥（`FF_FS_REENTRANT=1` 时需要）、堆分配（`FF_USE_LFN=3` 时需要）等样板，本工程 `FF_FS_REENTRANT=0`、`FF_USE_LFN=2`（栈缓冲），两者都不需要，故不引入。

## 4. 从零开始的移植步骤

### Step 1：锁定并获取官方源码

获取 R0.15 的五个文件（见第 2、3 节）。本工程锁定官方原版，不修改 FatFs 内部实现，只新增 `port/diskio.c` 一个移植文件。

### Step 2：创建目录 + 裁剪 ffconf.h

按第 3 节结构创建 `Middlewares/FATFS`，官方文件放入 `src`/`inc`。`ffconf.h` 是本工程的关键裁剪点，核心宏：

| 宏 | 值 | 说明 |
| -- | -- | ---- |
| `FF_MIN_SS` / `FF_MAX_SS` | 4096 / 4096 | **固定 4KB 逻辑扇区**，对齐 W25Q64 4KB 擦除粒度；两者相等时 FatFs 不调用 `GET_SECTOR_SIZE` |
| `FF_FS_TINY` | 1 | Tiny 模式：FIL 无私有缓冲，共用 FATFS 的 `win[4096]` 窗口，省 RAM |
| `FF_USE_LFN` | 2 | 长文件名，工作缓冲放**任务栈**（(255+1)×2 = 512B） |
| `FF_MAX_LFN` | 255 | LFN 全支持 |
| `FF_CODE_PAGE` | 437 | OEM 码页（美国英语）；FF_LFN_UNICODE=0 时文件名按单字节处理 |
| `FF_VOLUMES` | 1 | 单卷，卷号固定为 `"0:"` |
| `FF_USE_MKFS` | 1 | 使能 `f_mkfs`（首次使用必须格式化） |
| `FF_FS_NORTC` | 1 | 无 RTC，文件时间戳固定（2026/8/5）；无需实现 `get_fattime()` |
| `FF_FS_REENTRANT` | 0 | **关闭 FatFs 内部互斥**，线程安全交给下层 SFUD 锁 |
| `FF_USE_LABEL` | 1 | 使能 `f_getlabel` / `f_setlabel` |
| `FF_USE_FIND` | 1 | 使能 `f_findfirst` / `f_findnext` |

关键约束：**4KB 扇区必须在三处保持一致**——`ffconf.h` 的 `FF_MIN_SS/FF_MAX_SS`、`diskio.c` 的 `FATFS_SECTOR_SIZE`、`f_mkfs` 的 `au_size`（见 Step 4、5）。改一处漏一处会直接读写错乱。

### Step 3：FAL 分区表新增 fatfs 分区

`Middlewares/FAL/port/fal_cfg.h` 的静态分区表新增一行（8MB W25Q64 划分 7 个分区）：

~~~c
#define FAL_PART_TABLE                                                          \
{                                                                               \
    {FAL_PART_MAGIC_WORD,      "app", NOR_FLASH_DEV_NAME,                      0,           512UL*1024, 0}, \
    {FAL_PART_MAGIC_WORD, "download", NOR_FLASH_DEV_NAME,               512UL*1024,           512UL*1024, 0}, \
    {FAL_PART_MAGIC_WORD,      "log", NOR_FLASH_DEV_NAME,         1UL*1024UL*1024,     1UL*1024UL*1024, 0}, \
    {FAL_PART_MAGIC_WORD, "lvgl_res", NOR_FLASH_DEV_NAME,         2UL*1024UL*1024,     3UL*1024UL*1024, 0}, \
    {FAL_PART_MAGIC_WORD,    "fatfs", NOR_FLASH_DEV_NAME,         5UL*1024UL*1024,     1UL*1024UL*1024, 0}, \
    {FAL_PART_MAGIC_WORD,      "fdb", NOR_FLASH_DEV_NAME,         6UL*1024UL*1024,           512UL*1024, 0}, \
    {FAL_PART_MAGIC_WORD,    "rsvd", NOR_FLASH_DEV_NAME, 6UL*1024UL*1024 + 512UL*1024,   1536UL*1024, 0}, \
}
~~~

`fatfs` 分区：偏移 `5UL*1024*1024`（0x00500000）、大小 1MB，4KB 对齐。1MB 的大小是刻意选的——它决定簇数 = 256 → 自动 FAT12（见 Step 5）。

### Step 4：移植 port/diskio.c（FAL diskio 模式）

`Middlewares/FATFS/port/diskio.c` 实现 FatFs 需要的四个磁盘接口。核心常量：

~~~c
#define FATFS_SECTOR_SIZE   4096UL   /* 必须与 ffconf.h 的 FF_MIN_SS/FF_MAX_SS 一致 */
#define FATFS_PART_NAME     "fatfs"  /* FAL 分区名（fal_cfg.h 分区表） */
static const struct fal_partition *s_part = NULL;  /* 懒查询缓存 */
~~~

**disk_initialize** —— 懒查询 + 缓存分区句柄，无 SPI 事务：

~~~c
DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0) { return STA_NOINIT; }
    if (NULL == fatfs_partition()) { s_disk_inited = 0; return STA_NOINIT; }
    s_disk_inited = 1;
    return 0;
}
~~~

**disk_read** —— 逻辑扇区号 × 4096 得到 FAL 字节偏移，做越界检查：

~~~c
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    uint64_t addr = (uint64_t)sector * FATFS_SECTOR_SIZE;
    if ((addr + (uint64_t)count * FATFS_SECTOR_SIZE) > s_part->len)
        return RES_PARERR;
    ret = fal_partition_read(s_part, (uint32_t)addr, buff, (size_t)count * FATFS_SECTOR_SIZE);
    if (ret != (int)((size_t)count * FATFS_SECTOR_SIZE)) return RES_ERROR;
    return RES_OK;
}
~~~

**disk_write** —— 逐扇区「先擦 4KB 再写 4KB」。这是移植最关键的设计：FatFs 内部 `fs->win[]` 窗口保证 `disk_write` 收到**完整扇区**数据（部分扇区写入由 FatFs 先整扇读回、再整扇回写），所以无需 read-modify-write，天然对齐 W25Q64 的 4KB 擦除粒度：

~~~c
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    for (i = 0; i < count; i++)
    {
        uint32_t off = (uint32_t)addr + (uint32_t)i * FATFS_SECTOR_SIZE;
        ret = fal_partition_erase(s_part, off, FATFS_SECTOR_SIZE);      /* 擦 4KB */
        if (ret != (int)FATFS_SECTOR_SIZE) return RES_ERROR;
        ret = fal_partition_write(s_part, off, buff + (size_t)i * FATFS_SECTOR_SIZE, FATFS_SECTOR_SIZE);  /* 写 4KB */
        if (ret != (int)FATFS_SECTOR_SIZE) return RES_ERROR;
    }
    return RES_OK;
}
~~~

**disk_ioctl** —— 只实现 FatFs 需要的四个命令：

| 命令 | 返回值 | 用途 |
| ---- | ------ | ---- |
| `CTRL_SYNC` | `RES_OK`（NOR 写即同步，无需 flush） | `f_sync` / 关闭时刷写 |
| `GET_SECTOR_COUNT` | `s_part->len / 4096` = 256 | `f_mkfs` 计算卷大小 |
| `GET_SECTOR_SIZE` | 4096 | 固定扇区模式不会被调用（实现无害） |
| `GET_BLOCK_SIZE` | 1（单位：扇区数） | `f_mkfs` 的擦除块对齐 |

注意 `CTRL_SYNC` 以 `buff=NULL` 调用，所以 NULL 检查只能在各自 case 内做，不能在入口统一拦截。

### Step 5：用户层 user_fatfs.c（挂载 / 格式化 / 读写）

`User_Task/User_Fatfs/Src/user_fatfs.c` 是应用示例，也是理解用法的入口。全部对象**静态分配**，不占 FreeRTOS 堆：

~~~c
static FATFS  s_fs;                /* 文件系统对象（含 win[4096] 窗口） */
static FIL    s_fil;               /* 文件对象（TINY 模式无私有缓冲） */
static uint8_t s_work[8192];       /* f_mkfs 工作区（≥2×FF_MAX_SS），绝不放任务栈 */
static uint8_t s_wbuf[4096];       /* 写缓冲 */
static uint8_t s_rbuf[4096];       /* 读缓冲 */
~~~

挂载 + 首次格式化：

~~~c
/* 卷号 "0:"，opt=1 立即执行磁盘访问 */
fr = f_mount(&s_fs, "0:", 1);
if (FR_NO_FILESYSTEM == fr)        /* 未格式化 → f_mkfs */
{
    MKFS_PARM opt;
    memset(&opt, 0, sizeof(opt));
    opt.fmt     = FM_ANY | FM_SFD;   /* 自动选型 + 超软盘格式（无 MBR 分区表） */
    opt.n_fat   = 0;                 /* 自动（FAT12 用 1 份 FAT） */
    opt.align   = 0;                 /* 自动 → 用 GET_BLOCK_SIZE=1 扇区对齐 */
    opt.n_root  = 128;               /* FAT12 根目录 128 项 = 1 扇区（4096/32） */
    opt.au_size = FATFS_SECTOR_SIZE; /* 4096，显式 1 扇区/簇 */
    fr = f_mkfs("0:", &opt, s_work, sizeof(s_work));
    fr = f_mount(&s_fs, "0:", 1);    /* 格式化后重新挂载 */
}
~~~

**为什么是 FAT12**：1MB 分区 ÷ 4KB 扇区 = 256 扇区；`au_size=4096` → 256 簇。FAT12 最大约 4085 簇、FAT16 上限 65524、FAT32 需 ≥65525，256 远小于阈值，`f_mkfs` 自动选择 FAT12。8MB Flash 上物理上不可能达到 FAT32 所需簇数。

**线程安全**：`FF_FS_REENTRANT=0`，FatFs 层不加锁。安全性靠两条保证——(1) 所有 `f_*` 调用只在 `fatfs_test` 一个任务线程内执行；(2) 底层 SFUD 每次读写擦前自动 take 工程共享的 `s_port_mutex`（与 externflash / FAL 测试任务共用一把锁），串行化 SPI 总线。**SFUD 锁 take 依赖调度器已启动**，所以所有真实 Flash 读写必须在线程内、调度器启动后执行。

### Step 6：CMake 构建集成

FatFs 本体建成静态库，port + 用户任务进根 target：

- `cmake/stm32cubemx/CMakeLists.txt`：`FATFS_Src = src/ff.c + src/ffunicode.c`，建 `FATFS` OBJECT 库并 `target_link_libraries(FATFS PUBLIC stm32cubemx)`；include 路径含 `Middlewares/FATFS/inc`。
- 根 `CMakeLists.txt`：登记 `Middlewares/FATFS/port/diskio.c`、`User_Task/User_Fatfs/Src/user_fatfs.c`；include 路径含 `Middlewares/FATFS/inc`、`Middlewares/FATFS/port`、`User_Task/User_Fatfs/Inc`。

### Step 7：user_init 编排

`User_Task/User_Init/Src/user_init.c` 中，`user_fatfs_init()` 必须在 `user_fal_init()` 之后调用（fatfs 分区来自 FAL）：

~~~text
drv_adapter_port_externflash_register()   # 注册 Port 到 Wrapper
  → drv_adapter_wrapper_externflash_init() # SPI + 互斥锁就绪
  → user_externflash_init()                # ExternFlash 测试线程
  → user_fal_init()                        # fal_init() + 分区表打印 + 测试线程
  → user_flashdb_init()                    # FlashDB KVDB 测试线程
  → user_fatfs_init()                      # 创建 fatfs_test 线程
~~~

`user_fatfs_init()` 只在调度器启动前**创建线程**；真正的 `f_mount` / `f_mkfs` / 读写全部在线程内（调度器启动后）执行。测试线程进入后先 `osal_task_delay_ms(150)` 等系统稳定再碰 Flash。

## 5. 关键实现细节与坑

| 坑 | 现象 | 原因 | 解决 |
| -- | ---- | ---- | ---- |
| 4KB 扇区不一致 | 挂载后读写错乱 / 数据损坏 | `FF_MIN_SS/FF_MAX_SS`、`diskio.c` 的 `FATFS_SECTOR_SIZE`、`au_size` 三处必须一致 | 三处统一 4096，改任一处同步改另外两处 |
| 调度器前同步读写 | 任务启动前调 `f_mount`/`f_read` 直接崩溃 | SFUD 的 `spi->lock` take 互斥锁依赖调度器已运行 | 真实读写全部放任务线程内 |
| f_mkfs 工作区放任务栈 | 任务栈溢出 / HardFault | `f_mkfs` 需要 ≥2×FF_MAX_SS = 8KB 工作区 | 静态声明 `s_work[8192]`，绝不放栈 |
| 写放大约 5 倍 | 小文件写入偏慢、Flash 磨损快 | 4KB 扇区 = 4KB 擦除粒度，每次写扇区先擦 4KB | 轻量使用可接受；把数据攒满扇区再写 |
| 非原子写 | 写中途掉电，文件系统损坏 | `disk_write` 先擦后写，无事务保障 | 最坏重新 `f_mkfs`；FAT 层可接受 |
| 想用 FAT16/FAT32 | `f_mkfs` 仍是 FAT12 | 簇数由分区大小 / `au_size` 决定 | 增大分区或减小 `au_size`，使簇数超过对应阈值 |
| 文件名中文乱码 | `FF_CODE_PAGE=437` 不支持中文 | OEM 码页与文件名字节编码不匹配 | 需要中文文件名时改 `FF_CODE_PAGE=936` 并注意编码 |

## 6. 本次实际验证结果

| 项目 | 结果 |
| ---- | ---- |
| FatFs R0.15 源码编译 | 通过 |
| diskio.c（FAL 桥接）编译 | 通过 |
| CMake 构建 | 通过 |
| `f_mount` | 通过 |
| `f_mkfs`（FAT12） | 通过 |
| 文件写 / 读回校验 | 通过，4096 字节 memcmp 一致 |
| `f_getfree` 容量查询 | 通过 |
| 掉电持久化 | 待补验（测试线程默认上电先擦分区重测） |

RTT Viewer 实测输出（首次上电，含格式化路径；各测试线程并发运行，日志在 RTT 中交错）：

~~~text
[SFUD] Found a Winbond W25Q64CV flash chip. Size is 8388608 bytes.
[SFUD] sfud_extflash flash device initialized successfully.
[I/FAL] Flash Abstraction Layer (V1.0.0) initialize success.
I/FAL_TEST        [0.000] fal_init: OK (partitions=7)
I/FAL_TEST        [0.000] task create: OK
I/FLASHDB         [0.000] init: task create OK
I/FATFS           [0.000] init: task create OK
I/USER_INIT       [0.000] user_init() done

I/FATFS           [0.150] === FatFs Test ===
I/FATFS           [0.150] no filesystem, f_mkfs FAT12 ...
I/FATFS           [0.302] f_mkfs OK
I/FATFS           [0.304] f_mount OK
I/FATFS           [0.304] f_write OK (len=4096)
I/FATFS           [0.306] write/read verify OK (len=4096)
I/FATFS           [0.306] volume: total=1036288 B, free=1032192 B, free_clst=252
I/FATFS           [0.306] === FatFs Test PASS ===
~~~

再次上电不格式化时，`f_mkfs` 一段不会出现，直接走 `f_mount OK`。容量数字可直接验证：1MB 分区 FAT12 共 253 个数据簇（`n_fatent-2=253`），每簇 4KB，总容量 253×4096 = 1036288 B；测试后剩 252 簇 = 1032192 B。

除日志外，还有全局 `volatile` 观测变量供 J-Link mem32 读取，不依赖串口：

- `g_fatfs_init_result`：挂载/格式化结果，0 = PASS
- `g_fatfs_rw_result`：文件读写校验，0 = PASS
- `g_fatfs_free_clst`：剩余簇数（观测用）

## 7. 常见问题

| 现象 | 原因 | 处理 |
| ---- | ---- | ---- |
| `f_mount` 返回 `FR_NO_FILESYSTEM` | 分区从未格式化 | 首次使用先 `f_mkfs`（见 Step 5） |
| `f_mount` 返回 `FR_DISK_ERR` | 分区损坏 / 扇区配置不一致 | 检查 4KB 扇区三处一致性；必要时重新 `f_mkfs` |
| `fal_partition_find("fatfs")` 返回 NULL | FAL 分区表没有 `fatfs` 分区，或分区表被 `--gc-sections` 裁剪 | 检查 `fal_cfg.h`；链接脚本 KEEP `FalPartTable` |
| 任务卡死 | 调度器前同步做文件读写 | 读写移到任务线程内 |
| RTT 看不到 FATFS 日志 | elog 未初始化 / RTT 未启动 / 任务还没轮到 | 先 `app_elog_init()`，再开 RTT Viewer；FATFS 测试线程启动前有 150ms 延迟 |
| 写进去读出来数据不对 | `FF_MIN_SS` 与 `diskio.c` 扇区不一致 | 三处统一 4096 |
| 想读取断电前写的文件 | 每次上电测试线程把分区擦掉重测 | 关掉测试线程的上电擦除逻辑，或另写应用任务只挂载不格式化 |

## 8. Git 修改范围

本次 FATFS 移植涉及的路径（分支 `FATFS`，提交 `277a609`）：

~~~text
Middlewares/FATFS/...                              # FatFs R0.15 官方源码 + port/diskio.c
User_Task/User_Fatfs/...                           # 用户测试线程（user_fatfs.c / user_fatfs.h）
User_Task/User_Init/Src/user_init.c                # 追加 user_fatfs_init 编排
CMakeLists.txt                                     # port + 用户任务登记
cmake/stm32cubemx/CMakeLists.txt                   # FATFS 静态库 + include
~~~

查看差异：

~~~powershell
git log --oneline -3
git show --stat 277a609
~~~

## 9. 参考资料

- [FatFs 官方站点（elm-chan）](http://elm-chan.org/fsw/ff/00index_e.html)
- [FatFs 官方文档（zh_CN，含所有 API 说明）](http://elm-chan.org/fsw/ff/00index_e.html#features)
- [FatFs GitHub 镜像（ChaN/ff）](https://github.com/ChaN/ff)
- [W25Q64JV datasheet](https://www.winbond.com/resource-files/W25Q64JV%20RevJ%2003232021%20Plus.pdf)
- [FAL 官方仓库](https://github.com/armink/fal)
- [SFUD 官方仓库](https://github.com/armink/SFUD)

源码、API 和版本信息以官方仓库为准；本笔记中的分区表、扇区配置和锁设计来自本工程实际代码。

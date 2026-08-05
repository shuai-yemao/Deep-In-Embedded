---
title: FlashDB 嵌入式移植指南
date: 2026-08-05
tags:
  - 嵌入式
  - 中间件
  - FlashDB
  - STM32
  - FreeRTOS
  - Flash
  - W25Q64
  - FAL
  - SFUD
aliases:
  - FlashDB 嵌入式移植
---

# FlashDB 嵌入式移植指南

> [!summary] 这篇指南解决什么问题
> 把 FlashDB V2.2.0 通过 FAL → SFUD → SPI 链路移植到 STM32F411CEU6 + FreeRTOS 工程，接入外部 W25Q64 SPI Flash，并说明源码来源、分层架构、FAL 分区规划、SFUD/SPI 对接、构建集成和验证方法。

## 0. 先看结论

本工程已经完成以下集成：

- FlashDB V2.2.0 官方源码已放入 Middlewares/FlashDB。
- FAL V1.0.0、SFUD V1.1.0 同步移植，组成 FlashDB → FAL → SFUD → SPI → W25Q64 完整链路。
- 8MB W25Q64 已划分 7 个分区，其中 `fdb` 分区 512KB 专供 FlashDB KVDB 使用。
- 软件 SPI（PB12=CS, PB13=SCK, PB14=MISO, PB15=MOSI）已对接 SFUD。
- 两把互斥锁（数据库锁 + SPI 总线锁）已桥接 FreeRTOS。
- CMake 构建通过，RTT 实测 KVDB 初始化 + KV/blob 读写通过。
- 掉电持久化（`FLASHDB_ERASE_PART_ON_BOOT=0`）尚未在实物上断电验证。

"中间件移植成功"不等于"业务 KV 设计已完成"。当前只是跑通了读写闭环。

想了解移植完成后怎么读写 KV 数据，请看 [[FlashDB嵌入式使用手册]]。

## 1. FlashDB 是什么，为什么分层

FlashDB 是一个基于 Flash 的轻量级数据库，但它不直接操作 Flash——它通过抽象层访问。这样同一套代码可以跑在不同 Flash 上。

本工程的完整调用链：

~~~text
应用层     user_flashdb.c            fdb_kv_set/get_blob（数据库锁 s_flashdb_mutex）
             │
             ▼
FlashDB   src/fdb_kvdb.c             _fdb_flash_read/erase/write
             │  flashdb.h 依赖 <fdb_cfg.h> 和 <fal.h>
             ▼
FAL       src/fal_partition.c        fal_partition_find/read/write/erase
             │  ops 回调指向 nor_flash0
             ▼
FAL port  fal_flash_sfud_port.c      w25qxx_fal_* → sfud_read/sfud_write/sfud_erase
             │  （本层默认不加锁，靠 SFUD 内部锁）
             ▼
SFUD      src/sfud.c                 spi->lock/unlock + spi->wr = sfud_spi_write_read
             │  锁 = s_sfud_shared_mutex（桥接 Port 层 s_port_mutex）
             ▼
SFUD port drv_adapter_sfud_externflash.c  绑定 wr/lock/unlock，复用 Wrapper get_spi
             │
             ▼
Port      drv_adapter_port_externflash.c   软件 SPI 位带实现（PB12-15）
             │
             ▼
硬件      W25Q64 SPI NOR Flash
~~~

每一层只做一件事：

| 层 | 职责 |
| -- | ---- |
| FlashDB | KVDB/TSDB 的键值管理、CRC 校验、GC 回收 |
| FAL | Flash 抽象层：把"分区"概念提供给上层，屏蔽具体 Flash |
| SFUD | 串行 Flash 通用驱动：JEDEC 识别、页编程、扇区擦除 |
| SPI Port | 底层字节收发（本工程为 GPIO 位带软件 SPI） |
| FreeRTOS | 提供全部互斥原语，串行化多任务对 SPI 的访问 |

## 2. 官方源码地址

本工程使用官方仓库源码，三个库均为工程内确认的版本：

| 库 | 版本（源码内宏） | 官方仓库 | 说明 |
| -- | --------------- | ------- | ---- |
| FlashDB | `FDB_SW_VERSION` = "2.2.0" | [armink/FlashDB](https://github.com/armink/FlashDB) | 数据库本体 |
| FAL | `FAL_SW_VERSION` = "1.0.0" | [armink/fal](https://github.com/armink/fal) | Flash 抽象层 |
| SFUD | `SFUD_SW_VERSION` = "1.1.0" | [armink/SFUD](https://github.com/armink/SFUD) | 串行 Flash 驱动 |

FlashDB 官方源码按模式划分：

| 官方目录 | 作用 | 本工程是否使用 |
| ------- | ---- | ------------- |
| `src/fdb.c` | 数据库通用初始化与底层读写分发 | 是 |
| `src/fdb_kvdb.c` | KVDB 全部实现 | 是 |
| `src/fdb_tsdb.c` | TSDB 实现 | 编译但不初始化 |
| `src/fdb_utils.c` | CRC32、状态位等工具 | 是 |
| `inc/flashdb.h` | 公共 API 头 | 是 |
| `fdb_cfg.h` | 官方板级配置模板 | 否，改用 port 版 |

## 3. 本工程目录结构

~~~text
Middlewares/
├── FlashDB/
│   ├── inc/                    # 官方头文件
│   │   ├── flashdb.h
│   │   ├── fdb_def.h
│   │   └── fdb_low_lvl.h
│   ├── port/
│   │   └── fdb_cfg.h           # 板级配置（本工程裁剪点）
│   └── src/                    # 官方源码
│       ├── fdb.c
│       ├── fdb_kvdb.c
│       ├── fdb_tsdb.c
│       └── fdb_utils.c
├── FAL/
│   ├── inc/
│   │   ├── fal.h
│   │   └── fal_def.h
│   ├── port/
│   │   ├── fal_cfg.h           # 设备表 + 分区表
│   │   ├── fal_flash_sfud_port.c
│   │   └── fal_flash_sfud_port.h
│   └── src/                    # 官方源码
│       ├── fal.c
│       ├── fal_flash.c
│       └── fal_partition.c
└── SFUD/
    ├── inc/
    │   ├── sfud.h
    │   ├── sfud_def.h
    │   ├── sfud_flash_def.h
    │   └── sfud_cfg.h
    └── src/
        └── sfud.c
~~~

另外两个关键移植文件在 BSP 层：

- `Bsp/Porting/drv_adapter_sfud_externflash/Src/drv_adapter_sfud_externflash.c` —— SFUD 的 SPI port（sfud_spi_port_init）。
- `Bsp/Porting/drv_adapter_port_externflash/Src/drv_adapter_port_externflash.c` —— 软件 SPI + 共享互斥锁。

## 4. 从零开始的移植步骤

### Step 1：锁定并获取官方源码

获取 FlashDB、FAL、SFUD 三个仓库源码的核心文件（见第 2、3 节）。本工程锁定官方原版，不修改 FlashDB 内部实现，只新增 port 层文件。

### Step 2：创建中间件目录

按第 3 节结构创建 `Middlewares/FlashDB`、`Middlewares/FAL`、`Middlewares/SFUD`，官方源文件放入 `src`/`inc`，板级配置放入 `port`。

### Step 3：移植 FAL（分区表 + Flash 设备）

**3.1 配置分区表** `Middlewares/FAL/port/fal_cfg.h`。8MB W25Q64 划分 7 个分区，`fdb` 分区从保留区划出：

~~~c
#define NOR_FLASH_DEV_NAME  "norflash0"

#define FAL_FLASH_DEV_TABLE \
{                           \
    &nor_flash0,            \
}

#define FAL_PART_HAS_TABLE_CFG
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

分区规划一览（合计 0x800000 = 8MB，各分区 4KB 对齐）：

| 分区名 | 偏移 | 大小 | 用途 |
| ------ | ---- | ---- | ---- |
| `app` | 0x00000000 | 512KB | OTA 主区 |
| `download` | 0x00080000 | 512KB | OTA 下载槽 |
| `log` | 0x00100000 | 1MB | 日志循环区 |
| `lvgl_res` | 0x00200000 | 3MB | LVGL 资源下载 |
| `fatfs` | 0x00500000 | 1MB | FATFS |
| `fdb` | 0x00600000 | 512KB | FlashDB KV 存储 |
| `rsvd` | 0x00680000 | 1.5MB | 保留 |

**3.2 注册 Flash 设备** `Middlewares/FAL/port/fal_flash_sfud_port.c` 定义 `nor_flash0` 实例，提供 init/read/write/erase 四个 ops：

~~~c
struct fal_flash_dev nor_flash0 = {
    .name       = NOR_FLASH_DEV_NAME,
    .addr       = 0,
    .len        = 0,                        /* 运行时由 init 填充 */
    .blk_size   = 4096UL,                   /* 4KB 擦除粒度 */
    .ops        = {
        .init   = w25qxx_fal_init,
        .read   = w25qxx_fal_read,
        .write  = w25qxx_fal_write,
        .erase  = w25qxx_fal_erase,
    },
    .write_gran = 1,                        /* NOR flash */
};
~~~

关键点：

- `.len` 和 `.blk_size` 在 `w25qxx_fal_init` 里运行时从 SFUD 设备填充（`chip.capacity` / `chip.erase_gran`），这样同一套代码兼容 W25Q32/W25Q64。
- `w25qxx_fal_erase` 先把偏移/长度对齐到整 4KB 再调 `sfud_erase`，规避 SFUD 尾部不足粒度少擦的问题。
- `FAL_PORT_USE_MUTEX` 默认 0：SFUD 内部 lock/unlock 已串行化，本层不加锁，避免多把锁抢同一根 SPI 总线。

### Step 4：移植 SFUD（SPI 对接 + 共享锁）

SFUD 核心 `sfud.c` 在 `hardware_init()` 阶段无条件调用 `sfud_spi_port_init()`。该函数必须由工程实现，本工程在 `drv_adapter_sfud_externflash.c` 实现：

~~~c
sfud_err sfud_spi_port_init(sfud_flash *flash)
{
    spi_driver_interface_t *spi_if;

    /* 桥接 Port 层共享互斥锁：SFUD 与 ExternFlash 访问串行化 */
    s_sfud_shared_mutex = (osal_mutex_handle_t)
        drv_adapter_wrapper_externflash_get_mutex();

    /* 复用现有 SPI 通道（Wrapper getter → Port 层 s_port_spi） */
    spi_if = (spi_driver_interface_t *)drv_adapter_wrapper_externflash_get_spi();
    if (NULL == spi_if) { return SFUD_ERR_NOT_FOUND; }

    flash->spi.wr         = sfud_spi_write_read;
    flash->spi.lock       = sfud_spi_lock;
    flash->spi.unlock     = sfud_spi_unlock;
    flash->spi.user_data  = (void *)spi_if;
    flash->retry.times    = 30000U;
    flash->retry.delay    = NULL;

    return SFUD_SUCCESS;
}
~~~

要点：

- **共享锁设计**：SFUD 的 lock/unlock 桥接到 Port 层的 `s_port_mutex`，与 ExternFlash Handler 等共用一把锁，避免多把独立锁并发争抢 SPI 总线。SFUD 拿锁、Wrapper 直通、FAL 全部串行化在同一把锁上。
- `sfud_spi_write_read` 一次 CS 拉低内完成"发命令/地址 → 读数据"，读超长（>0xFFFF）时分片。
- 日志 `sfud_log_info` / `sfud_log_debug` 桥接到 `printf`（工程已重定向 SEGGER RTT）。
- 底层 SPI 是本工程既有的软件 SPI（GPIO 位带，PB12-15），`ADAPTER_PORT_SPI_MODE` 可切硬件 SPI2，但当前用 SW 模式。

### Step 5：配置 fdb_cfg.h

`Middlewares/FlashDB/port/fdb_cfg.h` 是 FlashDB 的板级裁剪点：

~~~c
/* 启用 KVDB（键值数据库） */
#define FDB_USING_KVDB

/* 启用 TSDB（时序数据库）——保留编译，不调用 fdb_tsdb_init */
#define FDB_USING_TSDB

/* 存储走 FAL 模式：FAL → SFUD → SPI → W25Q64 */
#define FDB_USING_FAL_MODE

/* W25Q64 为 NOR flash，按 bit 写，写粒度取 1 */
#define FDB_WRITE_GRAN  1

/* STM32F411 小端——不定义即小端，无需改动 */
/* #define FDB_BIG_ENDIAN */

/* 默认 FDB_PRINT == printf，已重定向 SEGGER RTT，无需覆盖 */
/* #define FDB_PRINT(...)  printf(__VA_ARGS__) */

/* 调试信息：默认关闭，避免 __FILE__ 全路径膨胀 .rodata */
/* #define FDB_DEBUG_ENABLE */
~~~

注意：本文件靠 include 路径被 `flashdb.h` 的 `#include <fdb_cfg.h>` 命中（见 Step 6），不是官方根目录那个模板。`FDB_DEBUG_ENABLE` 勿开，否则每个日志点都嵌入 `__FILE__` 完整路径，.rodata 会膨胀数十 KB。

### Step 6：CMake 构建集成

三个库分别建成静态库，挂在 `stm32cubemx` INTERFACE 库下（`cmake/stm32cubemx/CMakeLists.txt`）：

~~~text
FAL      fal.c + fal_flash.c + fal_partition.c
SFUD     sfud.c
FlashDB  fdb.c + fdb_kvdb.c + fdb_tsdb.c + fdb_utils.c
~~~

Include 路径必须包含（FlashDB 靠路径命中 port 版 `fdb_cfg.h`）：

~~~text
Middlewares/FlashDB/inc
Middlewares/FlashDB/port
Middlewares/FAL/inc
Middlewares/FAL/port
Middlewares/SFUD/inc
~~~

根 `CMakeLists.txt` 再登记各 port / BSP / 应用文件，例如：

~~~text
Middlewares/FAL/port/fal_flash_sfud_port.c
Bsp/Porting/drv_adapter_sfud_externflash/Src/drv_adapter_sfud_externflash.c
Bsp/Porting/drv_adapter_port_externflash/Src/drv_adapter_port_externflash.c
User_Task/User_Init/...   User_Task/User_Fal/...   User_Task/User_Flashdb/...
~~~

启用 `--gc-sections` 链接选项：TSDB 源码随编译但从未被引用，会被自动裁掉，不占 Flash 空间。全局编译宏 `ELOG`、`RTT` 使能日志输出。

### Step 7：链接脚本保留分区表

FAL 静态分区表放在自定义 section `FalPartTable`，若不处理会被 `--gc-sections` 裁掉，导致 `fal_partition_find` 永远找不到分区。链接脚本 `STM32F411XX_FLASH.ld` 必须显式 KEEP：

~~~text
  /* FAL partition table — 静态分区表（const），显式 KEEP 防 --gc-sections 裁剪 */
  .fal_part_table :
  {
    KEEP(*(FalPartTable))
  }
~~~

### Step 8：应用层 user_flashdb.c

初始化顺序（`user_init()`）：

~~~text
drv_adapter_port_externflash_register()   # 注册 Port 到 Wrapper
  → drv_adapter_wrapper_externflash_init() # SPI + 互斥锁就绪
  → user_externflash_init()                # ExternFlash 测试线程
  → user_fal_init()                        # fal_init() + 分区表打印 + 测试线程
  → user_flashdb_init()                    # 建锁 + 创建 KVDB 测试线程
~~~

`user_flashdb_init()` 只在调度器启动前**建互斥锁 + 建线程**；真正的 `fdb_kvdb_init` / KV 读写全部在线程内（调度器启动后）执行，因为 SFUD 的 SPI 锁 take 依赖调度器已运行：

~~~c
int user_flashdb_init(void)
{
    /* 建数据库层互斥锁（调度器前创建安全，take 才需任务上下文） */
    osal_mutex_create(&s_flashdb_mutex);

    /* 建测试线程：真正 Flash 读写在线程内、调度器启动后执行 */
    osal_task_create("flashdb_test", flashdb_test_entry,
                     FLASHDB_TEST_STACK_SIZE, FLASHDB_TEST_PRIORITY, ...);

    return 0;
}
~~~

线程内先注入锁再 init（init 内部也走 db_lock/db_unlock）：

~~~c
fdb_kvdb_control(&s_kvdb, FDB_KVDB_CTRL_SET_LOCK,   (void *)flashdb_lock);
fdb_kvdb_control(&s_kvdb, FDB_KVDB_CTRL_SET_UNLOCK, (void *)flashdb_unlock);
err = fdb_kvdb_init(&s_kvdb, "fdb_kvdb", "fdb", NULL, NULL);
~~~

测试线程可选在 bring-up 期上电擦分区保证确定性（`FLASHDB_ERASE_PART_ON_BOOT=1`），做掉电持久化验证时改成 0。

## 5. 关键实现细节与坑

| 坑 | 现象 | 原因 | 解决 |
| -- | ---- | ---- | ---- |
| 调度器前同步读写 | 任务启动前调用 `fdb_kv_set` 直接崩溃 | SFUD 的 `spi->lock` take 互斥锁依赖调度器已运行 | 真实读写全部放任务线程内 |
| 锁注入顺序 | init 正常但并发操作乱序 | `fdb_kvdb_init` 内部也走 lock/unlock | 先 `fdb_kvdb_control(SET_LOCK/SET_UNLOCK)` 再 init |
| 分区表被裁 | `fal_partition_find` 返回 NULL | `--gc-sections` 裁掉了 `FalPartTable` 段 | 链接脚本 `KEEP(*(FalPartTable))` |
| 配置不生效 | 改了 `fdb_cfg.h` 没反应 | include 路径没指向 port 目录 | 确认 `Middlewares/FlashDB/port` 在 include 路径里 |
| 软 SPI 慢 | 大块 blob 写入偏慢 | GPIO 位带逐 bit 翻转 | 实测够用则保留；性能敏感换硬件 SPI2 |
| TSDB 报链接错误 | 引用 TSDB 的符号找不到 | 只用 KVDB 时 TSDB 被 gc-sections 裁掉 | 需要 TSDB 时给 `fdb` 分区另划 TSDB 子区，别和 KVDB 共用 |

## 6. 本次实际验证结果

| 项目 | 结果 |
| ---- | ---- |
| FlashDB V2.2.0 源码编译 | 通过 |
| FAL V1.0.0 / SFUD V1.1.0 编译 | 通过 |
| `fal_init()` 分区识别 | 通过，7 个分区 |
| `fdb_kvdb_init` | 通过 |
| KV 字符串 set/get | 通过，`username -> firechip` |
| blob 写/读回校验 | 通过，64 字节 memcmp 一致 |
| CMake 构建 | 通过 |
| 掉电持久化 | 待做（需 `FLASHDB_ERASE_PART_ON_BOOT=0` 断电实测） |

RTT Viewer 实测输出：

~~~text
I/FLASHDB  [0.150] === FlashDB KV Test ===
I/FLASHDB  [0.150] erase fdb partition: OK
I/FLASHDB  [0.152] fdb_kvdb_init OK
I/FLASHDB  [0.152] kv set/get OK: firechip
I/FLASHDB  [0.154] blob set/get OK (len=64)
I/FLASHDB  [0.154] === FlashDB Test PASS ===
~~~

除日志外，还有全局 `volatile` 观测变量供 J-Link mem32 读取，不依赖串口：

- `g_flashdb_init_result`：KVDB 初始化结果，0 = PASS
- `g_flashdb_kv_result`：KV set/get 结果，0 = PASS
- `g_flashdb_blob_result`：blob 读写校验，0 = PASS

## 7. 常见问题

| 现象 | 原因 | 处理 |
| ---- | ---- | ---- |
| `Cannot open source input file fdb_cfg.h` | include 路径没配 | 增加 `Middlewares/FlashDB/port` |
| `fal_partition_find("fdb")` 返回 NULL | 分区表被 gc-sections 裁剪 | 链接脚本 KEEP `FalPartTable` |
| `fdb_kvdb_init` 返回非 0 | 分区不存在 / 分区小于 2 扇区 | 检查 `fal_cfg.h` 分区表与 `fdb` 分区大小 |
| 任务卡死 | 调度器前同步做 FlashDB 读写 | 读写移到任务线程内 |
| SFUD 报 JEDEC 识别失败 | SPI 接线 / 时序 / 片选问题 | 先单独验证 `sfud_init` 与 W25Q64 JEDEC ID |
| RTT 看不到 FlashDB 日志 | elog 未初始化 / RTT 未启动 | 先 `app_elog_init()`，再开 RTT Viewer |
| 改了 fdb_cfg.h 没变化 | include 路径命中了官方模板而非 port 版 | 确认 `Middlewares/FlashDB/port` 在最前 |

## 8. Git 修改范围

本次 FlashDB 移植涉及的路径（分支 `FlashDB`，共 5 个提交）：

~~~text
Middlewares/FlashDB/...                              # FlashDB 源码 + port/fdb_cfg.h
Middlewares/FAL/inc|src|port/...                     # FAL 源码 + fal_cfg.h + fal_flash_sfud_port
Middlewares/SFUD/inc|src/...                         # SFUD 源码
Bsp/Porting/drv_adapter_sfud_externflash/...         # SFUD SPI port（sfud_spi_port_init）
Bsp/Porting/drv_adapter_port_externflash/...         # 软件 SPI + s_port_mutex
Bsp/Wapper/drv_adapter_wrapper_externflash/...       # get_spi / get_mutex 分发
Bsp/Borad_drive/.../ExternFlash/...                  # core_gpio_spi、W25Qxx 驱动、Handler
User_Task/User_Init|User_Externflash|User_Fal|User_Flashdb/...
CMakeLists.txt
cmake/stm32cubemx/CMakeLists.txt
STM32F411XX_FLASH.ld
~~~

查看差异：

~~~powershell
git log --oneline -5
git show --stat <commit>
~~~

## 9. 参考资料

- [FlashDB 官方仓库](https://github.com/armink/FlashDB)
- [FlashDB 官方文档（zh_CN）](https://armink.github.io/FlashDB/#/zh-cn/)
- [FAL 官方仓库](https://github.com/armink/fal)
- [SFUD 官方仓库](https://github.com/armink/SFUD)
- [W25Q64 datasheet](https://www.winbond.com/resource-files/W25Q64JV%20RevJ%2003232021%20Plus.pdf)

源码、API 和版本信息以官方仓库为准；本笔记中的分区表、引脚和锁设计来自本工程实际代码。

---
title: SFUD 串行 Flash 通用驱动库移植与使用指南
date: 2026-08-05
tags:
  - 嵌入式
  - 中间件
  - SFUD
  - STM32
  - Flash
  - FreeRTOS
  - W25Q32
aliases:
  - SFUD 嵌入式移植
---

# SFUD 串行 Flash 通用驱动库移植与使用指南

> [!summary] 这篇指南解决什么问题
> 把 SFUD 串行 Flash 通用驱动库加入 STM32F411CEU6 + FreeRTOS + CMake 工程，并说明源码来源、文件位置、配置裁剪、SPI 移植层对接、线程安全设计和验证方法。本工程已完成该移植，FAL 已套接在 SFUD 之上（FAL → SFUD → SPI）。

## 0. 先看结论

本工程已经完成以下集成：

- 官方 SFUD v1.1.0 源码已放入 Middlewares/SFUD。
- 已裁剪不需要的功能：SFDP 解析、QSPI 模式、调试日志。
- 已实现 SPI 移植层 drv_adapter_sfud_externflash.c（替代官方 sfud_port.c 的角色）。
- SFUD 通过三个回调（wr / lock / unlock）注入底层 SPI 通道，全程无直接函数调用。
- 线程安全：SFUD 每次读写擦内部自动加锁，且与 ExternFlash 驱动栈共用同一把 OSAL 互斥锁。
- FAL 已套接在 SFUD 之上，分区读写擦经 SFUD 下发，构建通过。

"SFUD 移植成功"不等于"业务数据已经可靠落盘"。本指南只说明驱动层集成，FAL 分区表、日志存储等业务还需单独验证。

## 1. SFUD 是什么

SFUD 是 armink 开源的串行 SPI Flash 通用驱动库。市面上串行 Flash 种类繁多，各家厂商的读写擦命令、页大小、擦除粒度都不一致，SFUD 解决的就是"一套接口操作所有 SPI Flash"的问题。

它的工作方式：读芯片 JEDEC ID（如 W25Q32BV 为 `0xEF 0x40 0x16`），从内置芯片参数表查表得到容量、页大小、擦除命令，自动匹配对应的操作，因此**换一块 Flash 通常只需要改配置，不用改上层业务代码**。

本工程的分层结构：

~~~text
用户层（user_fal.c 测试任务）
    -> FAL（分区抽象：按名字找分区、偏移映射、越界检查）
        -> SFUD（芯片级驱动：JEDEC 识别、页编程、擦除、wait_busy 轮询）
            -> 移植层回调（wr / lock / unlock）
                -> spi_driver_interface_t（函数指针表）
                    -> 软件 SPI（GPIO PB12-15 位带） 或 硬件 SPI2
~~~

| 文件 | 作用 |
|---|---|
| sfud.c | SFUD 核心：芯片识别、读写擦、wait_busy、锁串行化 |
| sfud.h | 公共 API 声明 |
| sfud_def.h | 数据结构（sfud_flash / sfud_spi）、宏、错误码 |
| sfud_flash_def.h | 内置芯片参数表、厂商表 |
| sfud_cfg.h | 用户配置：功能裁剪开关 + 设备表 |

## 2. 官方源码地址

本次使用官方仓库的 `1.1.0` 标签（注意标签名没有 `v` 前缀），不使用本地缓存或自定义兼容实现。

- 官方仓库：[armink/SFUD](https://github.com/armink/SFUD)
- 版本标签：[SFUD 1.1.0](https://github.com/armink/SFUD/tree/1.1.0)
- 官方 README：[README.md](https://github.com/armink/SFUD/blob/1.1.0/README.md)
- 官方许可证：[LICENSE](https://github.com/armink/SFUD/blob/1.1.0/LICENSE)

官方 1.1.0 仓库目录结构：

~~~text
SFUD@1.1.0/
├── sfud/                核心库
│   ├── inc/             sfud.h / sfud_cfg.h / sfud_def.h / sfud_flash_def.h
│   ├── src/             sfud.c / sfud_sfdp.c
│   └── port/            sfud_port.c（官方移植模板）
├── demo/                各平台示例
├── docs/                文档
├── LICENSE              MIT 协议
└── README.md
~~~

官方文件标识（Git blob SHA，用于核对是否用了未经修改的上游文件）：

| 官方文件 | Git blob SHA |
|---|---|
| sfud/inc/sfud.h | 2b68c38216f707f8d20ae007529ec74c4e769ef4 |
| sfud/inc/sfud_def.h | c06f9b12954b115302b901569d3cace589ebd873 |
| sfud/inc/sfud_flash_def.h | 01c380b2e025880bffa5fc5ce52a4cffaef8a2a1 |
| sfud/inc/sfud_cfg.h | 1ea6cfdbd44c2f23011d5f0d32eeb102218e9104 |
| sfud/src/sfud.c | 2095eded3dec12928d00c44f95cd034eef0a387a |
| sfud/src/sfud_sfdp.c | e71cf96bca28cb62f14b735093be3a803a958fbf |
| sfud/port/sfud_port.c | bf2397966d5af2c228b35dc4d9ea6c43796b4208 |

说明：`sfud_cfg.h` 和 `sfud_port.c` 在本工程中都被本地化修改过（配置裁剪、移植层重写），SHA 仅供核对官方未修改文件。

## 3. 本工程目录结构

~~~text
STM32F411CEU6_FreeRTOS/
├── Middlewares/SFUD/                     SFUD 库本体
│   ├── inc/
│   │   ├── sfud.h
│   │   ├── sfud_cfg.h                    本地化配置（裁剪 SFDP/QSPI/DEBUG）
│   │   ├── sfud_def.h
│   │   └── sfud_flash_def.h
│   └── src/
│       └── sfud.c                        SFUD 核心（唯一 .c 源文件）
└── Bsp/porting/drv_adapter_sfud_externflash/   移植层（替代官方 sfud_port.c）
    ├── Inc/
    │   └── drv_adapter_sfud_externflash.h
    └── Src/
        └── drv_adapter_sfud_externflash.c
~~~

- Inc 和 Src：SFUD 库代码与本地配置。
- Bsp/porting 下的移植层：把 SFUD 抽象的 SPI 接口绑定到工程已有的 ExternFlash SPI 通道。

官方 sfud_port.c 在本工程中**没有使用**，其角色由 drv_adapter_sfud_externflash.c 承担。原因是工程已有完整的 SPI 驱动栈（wrapper → spi_driver_interface_t），无需再写一套裸机 SPI 位操作。

## 4. 从零开始的移植步骤

### Step 1：锁定并获取官方源码

只需要获取非 SFDP 的文件：

~~~text
sfud/inc/sfud.h
sfud/inc/sfud_cfg.h
sfud/inc/sfud_def.h
sfud/inc/sfud_flash_def.h
sfud/src/sfud.c
LICENSE
~~~

本工程锁定 armink/SFUD 的 `1.1.0` 标签。`sfud_sfdp.c` 与 SFDP 解析相关，本工程裁剪掉（见 Step 3 的 `SFUD_USING_SFDP`）。

### Step 2：创建中间件目录

~~~text
Middlewares/SFUD/
├── inc
└── src
~~~

官方头文件放进 inc，官方 sfud.c 放进 src。

### Step 3：配置 sfud_cfg.h

SFUD 的裁剪核心在 `sfud_cfg.h`。本工程的状态：

| 配置项 | 状态 | 说明 |
|---|---|---|
| `SFUD_DEBUG_MODE` | 关闭 | 调试期打开可输出 JEDEC 识别日志到 RTT |
| `SFUD_USING_FLASH_INFO_TABLE` | 开启 | 使用内置芯片参数表识别 W25Q32BV；SFDP 关闭后这是唯一识别途径 |
| `SFUD_USING_SFDP` | 关闭 | 不引入 sfud_sfdp.c |
| `SFUD_USING_QSPI` | 关闭 | 标准 SPI 模式 |
| `SFUD_EXTERNFLASH_DEVICE_INDEX` | 0 | 设备索引 |
| `SFUD_FLASH_DEVICE_TABLE` | 定义 | 单设备表，chip 字段留 0，运行时按 JEDEC ID 自动识别填入 |

设备表配置：

~~~c
#define SFUD_FLASH_DEVICE_TABLE \
{ \
    {"sfud_extflash", "extern_spi"}, \
}
~~~

设备名 `sfud_extflash`，SPI 名 `extern_spi`。**chip 字段全部留 0**，由 `hardware_init()` 在 `sfud_init()` 阶段读 JEDEC ID 后从内置表回填。

### Step 4：实现 SPI 移植层

SFUD 核心通过 `extern sfud_err sfud_spi_port_init(sfud_flash *flash);` 声明调用移植函数，在 `sfud_init()` 阶段被 `hardware_init()` 无条件调用。移植层职责：把 `flash->spi` 的抽象回调绑到本模块的静态函数。

~~~c
sfud_err sfud_spi_port_init(sfud_flash *flash)
{
    flash->spi.wr        = sfud_spi_write_read;   /* 全双工事务回调 */
    flash->spi.lock      = sfud_spi_lock;         /* 加锁回调 */
    flash->spi.unlock    = sfud_spi_unlock;       /* 解锁回调 */
    flash->spi.user_data = (void *)spi_if;        /* spi_driver_interface_t* */
    flash->retry.times   = SFUD_SPI_RETRY_TIMES;  /* 30000 次轮询上限 */
    flash->retry.delay   = NULL;                  /* 无阻塞延时 */

    return SFUD_SUCCESS;
}
~~~

关键配置宏：

| 宏 | 值 | 说明 |
|---|---|---|
| `SFUD_SPI_TIMEOUT_MS` | 1000UL | SPI 单次收发超时 |
| `SFUD_SPI_LOCK_TIMEOUT` | 0xFFFFFFFFUL | 互斥锁永久等待 |
| `SFUD_SPI_RETRY_TIMES` | 30000U | wait_busy 轮询上限，覆盖 4KB 扇区擦除最坏 400ms |
| `SFUD_READ_CHUNK_MAX` | 0xFFFFU | 读超长时按此分片（uint16_t 长度上限） |

### Step 5：实现全双工事务回调

`sfud_spi_write_read` 是 SPI 注入的核心，一次 CS 低电平内完成整笔事务：

~~~text
pf_cs_enable -> pf_write（发命令/地址/数据） -> pf_read（分片收数据，每片 0xFFFF，全程 CS 保持低） -> pf_cs_disable
~~~

底层 SPI 通道来源（`drv_adapter_port_externflash.c`）：

- 默认**软件 SPI**：GPIO PB12-15 位带模拟 SPI（`core_gpio_spi_*`）。
- 可选**硬件 SPI2**（定义 `ADAPTER_PORT_SPI_MODE_HW` 时）：`HAL_SPI_Transmit/Receive(&hspi2, ...)`，CS 用 PB12。

`user_data` 指向的 `spi_driver_interface_t`（定义于 bsp_w25qxx_driver.h）：

~~~c
typedef struct {
    w25qxx_status_t (*pf_init)(void);
    w25qxx_status_t (*pf_write)(uint8_t *data, uint16_t size, uint32_t timeout);
    w25qxx_status_t (*pf_read)(uint8_t *data, uint16_t size, uint32_t timeout);
    w25qxx_status_t (*pf_cs_enable)(void);
    w25qxx_status_t (*pf_cs_disable)(void);
} spi_driver_interface_t;
~~~

注意：`pf_write` 参数为 non-const `uint8_t*`，SFUD 传 const 缓冲需要强转。底层 `core_gpio_spi_write_byte` 不改缓冲，强转安全。

### Step 6：接入共享互斥锁

SFUD 核心内部不直接用 FreeRTOS 原语，而是通过 OSAL 抽象（`osal_mutex_take/give`）。本工程的关键设计是**与 ExternFlash 栈共用一把锁**：

~~~c
s_sfud_shared_mutex = (osal_mutex_handle_t)drv_adapter_wrapper_externflash_get_mutex();
~~~

这把锁是 Port 层的 `s_port_mutex`（`osal_mutex_create` 创建），经 Wrapper 的 getter 暴露。SFUD 与 Wrapper / ExternFlash 测试任务共用同一把锁，避免多把独立锁并发争抢 SPI 总线。锁通过 `sfud_spi` 的 lock/unlock 回调在每次读写擦操作前/后自动触发。

因此 `FAL_PORT_USE_MUTEX` 保持 0——锁已经下放到 SFUD 层，FAL 层不再重复加锁。

### Step 7：构建集成（CMake）

- 根 `CMakeLists.txt`：追加移植层源文件 `drv_adapter_sfud_externflash.c`，并加入 include 路径 `Middlewares/SFUD/inc`、`Bsp/Porting/drv_adapter_sfud_externflash/Inc`。
- `cmake/stm32cubemx/CMakeLists.txt`：将 `sfud.c` 单独编译为 OBJECT 库：

~~~cmake
set(SFUD_Src Middlewares/SFUD/src/sfud.c)
add_library(SFUD OBJECT ${SFUD_Src})
~~~

连同 FreeRTOS、OSAL、FAL、STM32_Drivers 一起链接进 stm32cubemx INTERFACE 库。

### Step 8：加入验证用例

本工程通过 `user_fal.c` 测试任务验证：`erase_all -> 写入 0x10.. 递增模式 -> 读回 memcmp 比对`。结果变量 `g_fal_part_count` / `g_fal_test_result` 可用 J-Link 观测。

## 5. 公共 API 一览

| 函数 | 说明 |
|---|---|
| `sfud_err sfud_init(void)` | 初始化所有 Flash 设备（触发 sfud_spi_port_init + 芯片识别） |
| `sfud_err sfud_device_init(sfud_flash *flash)` | 初始化单个 Flash 设备 |
| `sfud_flash *sfud_get_device(size_t index)` | 按索引取设备 |
| `size_t sfud_get_device_num(void)` | 设备数量 |
| `const sfud_flash *sfud_get_device_table(void)` | 设备表指针 |
| `sfud_err sfud_read(const sfud_flash *flash, uint32_t addr, size_t size, uint8_t *data)` | 读数据 |
| `sfud_err sfud_write(const sfud_flash *flash, uint32_t addr, size_t size, const uint8_t *data)` | 写数据（自动跨页，不自动擦除） |
| `sfud_err sfud_erase(const sfud_flash *flash, uint32_t addr, size_t size)` | 按粒度擦除 |
| `sfud_err sfud_erase_write(const sfud_flash *flash, uint32_t addr, size_t size, const uint8_t *data)` | 先擦后写 |
| `sfud_err sfud_chip_erase(const sfud_flash *flash)` | 整片擦除 |
| `sfud_err sfud_read_status(const sfud_flash *flash, uint8_t *status)` | 读状态寄存器 |
| `sfud_err sfud_write_status(const sfud_flash *flash, bool is_volatile, uint8_t status)` | 写状态寄存器 |

错误码枚举（sfud_def.h）：

| 枚举 | 值 | 含义 |
|---|---|---|
| `SFUD_SUCCESS` | 0 | 成功 |
| `SFUD_ERR_NOT_FOUND` | 1 | 未找到（芯片不在表内等） |
| `SFUD_ERR_WRITE` | 2 | 写错误 |
| `SFUD_ERR_READ` | 3 | 读错误 |
| `SFUD_ERR_TIMEOUT` | 4 | 超时 |
| `SFUD_ERR_ADDR_OUT_OF_BOUND` | 5 | 地址越界 |

## 6. 关键数据结构

**sfud_spi**（SPI 设备，回调注入点）：

~~~c
typedef struct __sfud_spi {
    char *name;
    sfud_err (*wr)(const struct __sfud_spi *spi, const uint8_t *write_buf,
                   size_t write_size, uint8_t *read_buf, size_t read_size);
    void (*lock)(const struct __sfud_spi *spi);
    void (*unlock)(const struct __sfud_spi *spi);
    void *user_data;
} sfud_spi;
~~~

**sfud_flash**（串行 Flash 设备）：

~~~c
typedef struct {
    char *name;
    size_t index;
    sfud_flash_chip chip;   /* 芯片参数 */
    sfud_spi spi;           /* SPI 设备（含 wr/lock/unlock 回调） */
    bool init_ok;
    bool addr_in_4_byte;
    struct { void (*delay)(void); size_t times; } retry;
    void *user_data;
} sfud_flash;
~~~

**sfud_flash_chip**（芯片识别参数，识别后回填）：

~~~c
typedef struct {
    char *name;
    uint8_t mf_id, type_id, capacity_id;
    uint32_t capacity;          /* 容量（字节） */
    uint16_t write_mode;        /* SFUD_WM_PAGE_256B / SFUD_WM_BYTE / SFUD_WM_AAI / SFUD_WM_DUAL_BUFFER */
    uint32_t erase_gran;        /* 擦除粒度（字节） */
    uint8_t erase_gran_cmd;     /* 擦除命令 */
} sfud_flash_chip;
~~~

本工程芯片 W25Q32BV（内置表 `SFUD_FLASH_CHIP_TABLE`）：

~~~text
{"W25Q32BV", SFUD_MF_ID_WINBOND, 0x40, 0x16, 4L*1024L*1024L, SFUD_WM_PAGE_256B, 4096, 0x20}
~~~

即：256B 页编程、4KB 擦除粒度、扇区擦除命令 `0x20`、容量 4MB。

## 7. 线程安全设计

SFUD 核心在每次读 / 写 / 擦 / 整片擦 / 先擦后写前都调用 `spi->lock(spi)`，完成后调用 `spi->unlock(spi)`。本工程把这两个回调绑定到共享互斥锁：

- 锁对象：Port 层 `s_port_mutex`（OSAL 互斥锁），经 Wrapper 的 `get_mutex` 桥接暴露。
- 共享范围：SFUD 与 ExternFlash Wrapper / 测试任务共用同一把锁，多把独立锁会并发争抢 SPI 总线。
- FAL 层不重复加锁：`FAL_PORT_USE_MUTEX` 保持 0。

多任务场景：任何线程通过 SFUD（或经 FAL）访问 Flash 都会自动串行化，业务代码不需要手动加锁。

## 8. 初始化时序

~~~text
main() -> user_init()                                     Core/Src/main.c
    -> drv_adapter_port_externflash_register()            Port 注册到 Wrapper
    -> drv_adapter_wrapper_externflash_init()             初始化 SPI/Driver/Handler，创建 s_port_mutex
    -> user_externflash_init()                            创建 ExternFlash 测试任务
    -> user_fal_init() -> fal_init()                      FAL 初始化，触发 ops.init
        -> w25qxx_fal_init() -> sfud_init()               SFUD 核心初始化
            -> sfud_device_init() -> hardware_init()
                -> sfud_spi_port_init()                   【SFUD 移植层】绑定 wr/lock/unlock 回调
~~~

全部在**调度器启动前**完成。因此移植层 init 阶段禁止 `HAL_Delay` / `osal_task_delay`（纯 GPIO 位带 SPI 事务），否则会阻塞启动流程。

## 9. 本次实际验证结果

| 项目 | 结果 |
|---|---|
| 官方 SFUD 1.1.0 源码编译（SFUD OBJECT 库） | 通过 |
| W25Q32BV JEDEC 识别（`0xEF 0x40 0x16`，4MB / 4096B） | 通过内置芯片表自动识别 |
| 裁剪配置（关 SFDP/QSPI/DEBUG，开 FLASH_INFO_TABLE） | 编译通过 |
| FAL → SFUD → SPI 全链路（app/download/param/log 分区） | 构建通过，测试任务逻辑通过 |
| 共享互斥锁接入（s_port_mutex） | 编译通过 |
| STM32 实物落盘验证 | 待实物烧录，通过 J-Link RTT 观测 `g_fal_test_result` 确认 |

"移植成功"与"实物验证通过"要分开看：上面的编译与逻辑验证已完成，最后一项建议在实物板上完成。

## 10. 常见问题

| 现象 | 原因 | 处理 |
|---|---|---|
| 芯片识别失败（返回 `SFUD_ERR_NOT_FOUND`） | SFDP 和内置表都关闭，或芯片不在表内 | 开启 `SFUD_USING_FLASH_INFO_TABLE`，确认 JEDEC ID 在 `SFUD_FLASH_CHIP_TABLE` 中 |
| Undefined symbol sfud_spi_port_init | SFUD 核心引用移植函数，未实现 | 提供 `sfud_spi_port_init(sfud_flash *flash)` 并绑定 wr/lock/unlock |
| init 阶段卡死 / 复位 | 移植层用了阻塞延时 | init 阶段禁 `HAL_Delay` / `osal_task_delay` |
| 擦除 4KB 超时失败 | wait_busy 轮询次数不足 | 增大 `SFUD_SPI_RETRY_TIMES`（本工程 30000） |
| 读回数据与写入不一致 | 未先擦除（Flash 只能 1 写 0），或跨页边界 | 先擦后写；SFUD 自动跨页，但页边界前数据需已擦 |
| 多任务并发读写擦异常 | 多把独立锁，未串行化 SPI 总线 | 与 ExternFlash 栈共用一把锁（s_port_mutex） |
| 地址越界返回 `SFUD_ERR_ADDR_OUT_OF_BOUND` | 偏移 + 长度超过设备容量 | 检查 addr + size 是否 ≤ 4MB |

## 11. Git 修改范围

~~~text
Middlewares/SFUD/inc/sfud.h
Middlewares/SFUD/inc/sfud_cfg.h
Middlewares/SFUD/inc/sfud_def.h
Middlewares/SFUD/inc/sfud_flash_def.h
Middlewares/SFUD/src/sfud.c
Bsp/porting/drv_adapter_sfud_externflash/Inc/drv_adapter_sfud_externflash.h
Bsp/porting/drv_adapter_sfud_externflash/Src/drv_adapter_sfud_externflash.c
Bsp/porting/drv_adapter_fal_externflash/Src/drv_adapter_fal_externflash.c
Bsp/porting/drv_adapter_fal_externflash/Inc/fal_cfg.h
User_Task/User_Fal/Src/user_fal.c
CMakeLists.txt
cmake/stm32cubemx/CMakeLists.txt
~~~

SFUD 库本体仅做配置裁剪，核心逻辑保持上游不动；工程所有定制都在 Bsp/porting 移植层。查看差异：

~~~powershell
git status --short
git diff --stat
git diff --check
~~~

本次移植的核心差异（与工程既有 SPI 栈对接）：

~~~diff
+ sfud_err sfud_spi_port_init(sfud_flash *flash)
+ {
+     flash->spi.wr        = sfud_spi_write_read;
+     flash->spi.lock      = sfud_spi_lock;
+     flash->spi.unlock    = sfud_spi_unlock;
+     flash->spi.user_data = (void *)spi_if;
+     flash->retry.times   = SFUD_SPI_RETRY_TIMES;
+     flash->retry.delay   = NULL;
+     return SFUD_SUCCESS;
+ }
~~~

移植层只新增回调绑定，底层 SPI 通道和互斥锁仍复用工程现有实现。

## 12. 参考资料

本文结构参考公开教程常见的"下载源码 → 整合工程 → 配置裁剪 → 移植层对接 → 线程安全 → 运行验证"顺序：

- [armink/SFUD 官方仓库](https://github.com/armink/SFUD)
- [SFUD 官方 README](https://github.com/armink/SFUD/blob/1.1.0/README.md)
- [armink 博客：SFUD 系列 Flash 通用驱动库](https://blog.csdn.net/armink)
- [W25Q32 数据手册（Winbond）](https://www.winbond.com/resource-files/W25Q32JV%20RevH%2009232021%20Plus.pdf)

博客用于参考移植思路；源码、API 和版本信息以官方仓库为准。

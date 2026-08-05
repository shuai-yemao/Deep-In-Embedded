---
title: SFUD 嵌入式使用手册
date: 2026-08-05
tags:
  - 嵌入式
  - SFUD
  - STM32
  - Flash
  - FreeRTOS
  - W25Q32
aliases:
  - SFUD 使用方法
---

# SFUD 嵌入式使用手册

> [!summary] 适合谁阅读
> 这是一份给第一次接触 SFUD 串行 Flash 驱动库的嵌入式开发者的入门手册。你不需要先弄懂 JEDEC ID、SFDP 或各家 Flash 的命令，先照着最小示例把一段数据写进去再读出来即可。

本手册讲 " 移植完成以后怎么用 SFUD"。如果你还没有把 SFUD 放进工程，请先看：

[[SFUD嵌入式移植指南]]

## 1. SFUD 到底是什么

SFUD 是一个 " 翻译官 "。市面上的 SPI Flash 芯片有很多家（Winbond、GigaDevice、ISSI……），每家的命令、页大小、擦除粒度都不一样。SFUD 通过读取芯片的 JEDEC ID，自动查表匹配对应的命令，让你用同一套 API 操作不同芯片。

它帮我们回答一个问题：

> 我要在 Flash 的这个地址，写这段数据，再读回来，对不对？

比如本工程的 W25Q32BV，容量 4MB，页大小 256B，扇区擦除 4KB。你不必记住它的扇区擦除命令是 `0x20`，只需调用：

~~~c
sfud_erase(flash, 0x000000, 4096);
~~~

SFUD 会自己决定怎么擦。

## 2. 先看一次完整读写

下面这段代码是 SFUD 最小可用的完整流程：初始化 → 取设备 → 擦除 → 写入 → 读回。

~~~c
#include "sfud.h"
#include <string.h>

void sfud_demo(void)
{
    sfud_flash *flash;
    uint8_t write_buf[] = {0x10, 0x11, 0x12, 0x13, 0x14};
    uint8_t read_buf[sizeof(write_buf)];

    /* 1. 初始化 SFUD（触发芯片识别） */
    if (sfud_init() != SFUD_SUCCESS) {
        return;             /* 初始化失败，芯片可能未接好 */
    }

    /* 2. 取第一个 Flash 设备 */
    flash = sfud_get_device(0);
    if (flash == NULL || !flash->init_ok) {
        return;
    }

    /* 3. 擦除目标扇区（Flash 必须先擦后写） */
    if (sfud_erase(flash, 0x000000, 4096) != SFUD_SUCCESS) {
        return;
    }

    /* 4. 写入数据（SFUD 自动按页编程，不需要自己拆页） */
    if (sfud_write(flash, 0x000000, sizeof(write_buf), write_buf) != SFUD_SUCCESS) {
        return;
    }

    /* 5. 读回比对 */
    memset(read_buf, 0, sizeof(read_buf));
    if (sfud_read(flash, 0x000000, sizeof(read_buf), read_buf) != SFUD_SUCCESS) {
        return;
    }

    /* memcmp 相同，说明读写闭环 */
}
~~~

这段代码分成五个部分：

| 代码 | 白话解释 |
|---|---|
| `sfud_init()` | 认识这块 Flash：读 ID、查表、填参数 |
| `sfud_get_device(0)` | 拿到设备句柄，之后所有操作都基于它 |
| `sfud_erase(flash, addr, size)` | 把目标区域擦成全 `0xFF` |
| `sfud_write(flash, addr, size, buf)` | 把数据写进去 |
| `sfud_read(flash, addr, size, buf)` | 把数据读出来，比对验证 |

## 3. 基本流程

SFUD 的操作顺序是固定的：

~~~text
sfud_init() -> sfud_get_device() -> sfud_erase() -> sfud_write() -> sfud_read()
~~~

一切数据写入都必须遵守一个铁律：

> **Flash 只能把 1 写成 0，不能把 0 写成 1。** 所以写之前必须先擦除，把目标区域恢复成全 1（`0xFF`）。

举例：某地址当前是 `0x00`，你想写 `0xFF`。直接写会失败，因为 0 变不回 1。只有先擦除（变 `0xFF`），才能再写。

## 4. 常用 API

### 4.1 初始化与取设备

~~~c
sfud_err sfud_init(void);
sfud_flash *sfud_get_device(size_t index);
~~~

`sfud_init()` 全局只调用一次，一般放在系统启动早期。成功后用 `sfud_get_device(0)` 取第一个设备。

### 4.2 读数据

~~~c
sfud_err sfud_read(const sfud_flash *flash, uint32_t addr, size_t size, uint8_t *data);
~~~

例子：读地址 `0x000000` 开始的 16 字节：

~~~c
uint8_t buf[16];
sfud_read(flash, 0x000000, sizeof(buf), buf);
~~~

读不需要先擦除，Flash 任何时候都能读。

### 4.3 写数据

~~~c
sfud_err sfud_write(const sfud_flash *flash, uint32_t addr, size_t size, const uint8_t *data);
~~~

例子：把配置结构体写到 Flash：

~~~c
typedef struct {
    uint8_t magic;
    uint16_t version;
    uint32_t checksum;
} config_t;

config_t cfg = {0xAA, 0x0001, 0x12345678};
sfud_write(flash, 0x100000, sizeof(cfg), (const uint8_t *)&cfg);
~~~

注意两点：

- `sfud_write` **不自动擦除**。跨页边界它会自动拆成多次页编程，但前提是目标区域已经擦过。
- 写入长度超过页大小（256B）时，SFUD 自动处理页边界，你不用自己拆。

### 4.4 擦除

~~~c
sfud_err sfud_erase(const sfud_flash *flash, uint32_t addr, size_t size);
sfud_err sfud_chip_erase(const sfud_flash *flash);
~~~

- `sfud_erase`：按芯片擦除粒度擦一段。W25Q32 粒度是 4KB，擦除会按粒度自动对齐（向上取整，注意尾部的量会被一起擦掉）。
- `sfud_chip_erase`：整片擦除，4MB 全变 `0xFF`。耗时较长，一般只在出厂或 OTA 前用。

### 4.5 先擦后写

~~~c
sfud_err sfud_erase_write(const sfud_flash *flash, uint32_t addr, size_t size, const uint8_t *data);
~~~

一步完成 " 擦除 + 写入 "。适合覆盖整块区域的场景，省去手动先擦。

### 4.6 读写状态寄存器

~~~c
sfud_err sfud_read_status(const sfud_flash *flash, uint8_t *status);
sfud_err sfud_write_status(const sfud_flash *flash, bool is_volatile, uint8_t status);
~~~

一般业务用不到，调试或设置保护位（如 W25Q32 的 BP0-BP3 写保护）时使用。

## 5. 页编程与擦除粒度

本工程 W25Q32BV 的关键参数：

| 参数 | 值 |
|---|---|
| 容量 | 4MB（32Mbit） |
| 页大小 | 256B |
| 擦除粒度（扇区） | 4KB |
| 扇区擦除命令 | `0x20` |

实际使用中的坑：

1. **写超页不用管**：SFUD 自动按页编程，但页边界处如果前一页没擦干净，写入会失败或数据错乱。
2. **擦除粒度不对齐会多擦**：`sfud_erase` 按粒度对齐，如果 `size` 不是 4096 的倍数，末尾一整块扇区都会被擦掉。本工程 FAL 对接层在 `w25qxx_fal_erase` 里先按 `FAL_W25QXX_SECTOR_SIZE = 4096` 对齐 start/end 再调用 SFUD，规避尾部少擦。
3. **寿命概念**：Flash 有擦写寿命（W25Q32 约 10 万次/扇区），频繁擦写的区域要考虑磨损均衡或放到特定分区。

## 6. 在 FAL 中的位置

本工程 SFUD 不是直接被业务调用的，而是作为 FAL 的底层驱动：

~~~text
用户（user_fal.c 测试任务）
    -> FAL 分区 API（按名字找分区，如 "app" / "log"）
        -> FAL 设备 norflash0（w25qxx_fal_* 回调）
            -> SFUD（sfud_read / sfud_write / sfud_erase）
                -> SPI 移植层回调 -> 软件 SPI / 硬件 SPI2
~~~

FAL 分区表（OTA 风格划分 4MB）：

| 分区 | 设备偏移 | 大小 |
|---|---|---|
| app | 0x000000 | 1MB |
| download | 0x100000 | 1MB |
| param | 0x200000 | 512KB |
| log | 0x280000 | 1.5MB |

如果你只想存一小块参数，可以直接用 FAL 分区接口，不用关心 Flash 细节：

~~~c
#include "fal.h"

void save_log_part(void)
{
    const struct fal_partition *part = fal_partition_find("log");
    uint8_t data[64];

    fal_partition_erase_all(part);                    /* 擦整个 log 分区 */
    fal_partition_write(part, 0, sizeof(data), data); /* 写入 */
    fal_partition_read(part, 0, sizeof(data), data);  /* 读回 */
}
~~~

用 FAL 的好处：地址写分区名，不写魔法数；分区边界和越界检查由 FAL 保证。

## 7. 多任务下的注意事项

本工程跑 FreeRTOS，SFUD 在多任务下使用要注意：

1. **锁是自动的，别重复加锁**：SFUD 每次读写擦内部都会调 `spi->lock` / `spi->unlock`，且和 ExternFlash 栈共用一把互斥锁（`s_port_mutex`）。业务代码直接调用即可，不需要自己再加锁。`FAL_PORT_USE_MUTEX` 保持 0，也是因为锁已经下放到 SFUD。
2. **不要在中断里调用**：SFUD 的 wait_busy 轮询和互斥锁都可能阻塞，必须在任务上下文调用。
3. **初始化必须在调度器启动前完成**：本工程在 `user_init()`（调度器启动前）通过 `fal_init() -> sfud_init()` 完成初始化。移植层 init 阶段也不允许阻塞延时。

## 8. 初学者最容易遇到的问题

| 现象 | 用白话说原因 | 怎么处理 |
|---|---|---|
| 写进去再读出来，数据不对 | 忘了先擦除，Flash 0 写不回 1 | 写之前先 `sfud_erase`，或改用 `sfud_erase_write` |
| 返回 `SFUD_ERR_ADDR_OUT_OF_BOUND` | 地址 + 长度超出 4MB 容量 | 检查 addr 和 size，确保 addr + size ≤ 0x400000 |
| 擦除后相邻数据也丢了 | 擦除粒度 4KB，尾部整扇区被一起擦 | 长度按 4096 对齐；重要数据按扇区规划位置 |
| 多任务时偶尔读写失败 | 多个线程并发访问 SPI 总线 | SFUD 锁是自动的；确认所有访问都走 SFUD/FAL，不要绕过它直连 SPI |
| 初始化失败、拿不到设备 | 芯片没接好、SPI 通道没初始化，或芯片不在表内 | 检查接线与 SPI 配置；打开 `SFUD_DEBUG_MODE` 看识别日志 |
| 写入超时 | wait_busy 轮询次数不够（擦除 4KB 最坏 400ms） | 确认 `SFUD_SPI_RETRY_TIMES` 足够（本工程 30000） |
| 每次上电数据丢失 | 用的是 RAM 临时缓冲，或擦写没真正完成 | 确认调用了 `sfud_write` 且返回成功；读回 memcmp 验证 |

## 9. 记住这五句话

1. SFUD 帮你 " 翻译 " 不同厂家的 SPI Flash，让你用同一套 API。
2. 固定顺序：初始化 → 取设备 → 擦除 → 写入 → 读回。
3. Flash 只能 1 写 0，**写之前必须先擦除**。
4. 跨页、粒度对齐这些细节 SFUD 自动处理，但 " 擦除会多擦尾部 " 要自己注意。
5. 多任务安全由共享互斥锁保证，业务代码不用手动加锁，但别绕过 SFUD 直连 SPI。

## 10. 继续学习

- [armink/SFUD 官方仓库](https://github.com/armink/SFUD)
- [SFUD 官方 README（含架构图与移植说明）](https://github.com/armink/SFUD/blob/1.1.0/README.md)
- [SFDP 解析（本项目已裁剪，需要自动探测新芯片时可开启 `SFUD_USING_SFDP`）](https://github.com/armink/SFUD/blob/1.1.0/sfud/src/sfud_sfdp.c)
- [W25Q32 数据手册](https://www.winbond.com/resource-files/W25Q32JV%20RevH%2009232021%20Plus.pdf)

如果某个 API、错误码或读写现象看不懂，把代码和返回的 `sfud_err` 贴出来，按 " 初始化 → 擦除 → 写入 → 读回 " 的顺序一起分析。

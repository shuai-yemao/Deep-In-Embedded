---
title: FlashDB 嵌入式使用手册
date: 2026-08-05
tags:
  - 嵌入式
  - 中间件
  - FlashDB
  - STM32
  - FreeRTOS
  - Flash
  - W25Q64
aliases:
  - FlashDB 使用方法
---

# FlashDB 嵌入式使用手册

> [!summary] 适合谁阅读
> 这是一份给第一次接触 FlashDB 的嵌入式开发者的入门手册。你不需要先学会 FAL、SFUD 或复杂的分区管理，先照着最小示例把一条 KV 数据写进 Flash 再读出来即可。

本手册讲 " 移植完成以后怎么用 FlashDB"。如果你还没有把 FlashDB 放进工程，请先看：

[[FlashDB嵌入式移植指南]]

## 1. FlashDB 到底是什么

FlashDB 是一个跑在 Flash 上的 " 数据库 "。它帮我们解决一个问题：

> MCU 掉电以后，我要记住的那些数据怎么不丢？

我们习惯把变量放在内存里，但一断电内存就清空了。FlashDB 做的事情，就是把数据按固定的格式写进外置 SPI Flash（本项目是 W25Q64），下次开机再读回来。

它提供了两种库：

| 数据库 | 全称 | 干什么用 | 数据长什么样 |
| ----- | ---- | ------ | ---------- |
| KVDB | Key-Value Database | 存 " 一个键对应一个值 " | `"username" -> "firechip"` |
| TSDB | Time Series Database | 按时间顺序存传感器数据 | 一条带时间戳的日志 |

本工程实际使用 KVDB。TSDB 源码随组件编译，但没有初始化实例（见第 9 节）。

可以把 KVDB 理解成 "Flash 上的简易键值表 "：

~~~text
FlashDB KVDB
  ├── username  ->  firechip
  ├── calib     ->  64 字节二进制标定数据
  └── password  ->  123456
~~~

每次写入都会在 Flash 上落一个带 CRC 校验的记录，掉电重启后依然在。

## 2. 先看一次完整测试

本工程在 `User_Task/User_Flashdb/Src/user_flashdb.c` 里有一个现成的 KVDB 测试线程，是理解 FlashDB 用法的最佳入口。它做了三件事：注入锁、初始化数据库、读写 KV。

~~~c
#include "flashdb.h"
#include "fal.h"

static struct fdb_kvdb s_kvdb;   /* KVDB 实例，静态分配，约 1.2KB */

/* 数据库层加锁/解锁回调（注入给 FlashDB，多任务安全） */
static void flashdb_lock(fdb_db_t db)
{
    (void)db;
    (void)osal_mutex_take(s_flashdb_mutex, OSAL_MAX_DELAY);
}
static void flashdb_unlock(fdb_db_t db)
{
    (void)db;
    (void)osal_mutex_give(s_flashdb_mutex);
}

void flashdb_test_entry(void)
{
    fdb_err_t err;
    struct fdb_blob blob;
    const char *value;
    uint8_t wbuf[64], rbuf[64];

    /* 先把锁注入数据库，再初始化（init 内部也要加锁） */
    fdb_kvdb_control(&s_kvdb, FDB_KVDB_CTRL_SET_LOCK,   (void *)flashdb_lock);
    fdb_kvdb_control(&s_kvdb, FDB_KVDB_CTRL_SET_UNLOCK, (void *)flashdb_unlock);

    /* 初始化 KVDB，第二个参数是数据库名，第三个参数是 FAL 分区名 */
    err = fdb_kvdb_init(&s_kvdb, "fdb_kvdb", "fdb", NULL, NULL);
    if (FDB_NO_ERR != err)
    {
        /* 初始化失败 */
    }

    /* KV 字符串写入 + 读取 */
    fdb_kv_set(&s_kvdb, "username", "firechip");
    value = fdb_kv_get(&s_kvdb, "username");
    /* value == "firechip" */

    /* blob 二进制写入 + 读取回校验 */
    fdb_kv_set_blob(&s_kvdb, "calib", fdb_blob_make(&blob, wbuf, sizeof(wbuf)));
    size_t rlen = fdb_kv_get_blob(&s_kvdb, "calib", fdb_blob_make(&blob, rbuf, sizeof(rbuf)));
    /* rlen == sizeof(wbuf)，且 memcmp(wbuf, rbuf) == 0 */
}
~~~

这段代码分成四步：

| 代码 | 白话解释 |
| --- | ------- |
| `fdb_kvdb_control(... SET_LOCK / SET_UNLOCK)` | 告诉 FlashDB " 多任务访问时用哪把锁 " |
| `fdb_kvdb_init(&s_kvdb, "fdb_kvdb", "fdb", ...)` | 打开 FAL 的 `fdb` 分区，初始化为 KVDB |
| `fdb_kv_set / fdb_kv_get` | 写入一条字符串 KV、读回来 |
| `fdb_kv_set_blob / fdb_kv_get_blob` | 写入一段二进制数据、读回来校验 |

在 J-Link RTT Viewer 里会看到这样的输出：

~~~text
I/FLASHDB  [0.150] === FlashDB KV Test ===
I/FLASHDB  [0.150] erase fdb partition: OK
I/FLASHDB  [0.152] fdb_kvdb_init OK
I/FLASHDB  [0.152] kv set/get OK: firechip
I/FLASHDB  [0.154] blob set/get OK (len=64)
I/FLASHDB  [0.154] === FlashDB Test PASS ===
~~~

## 3. KVDB 初始化

### 3.1 数据库对象要静态分配

`struct fdb_kvdb` 内部包含一个 64 项的 KV 缓存表，整个结构大约占 **1.2KB**。请把它声明成全局变量或静态变量，**不要放进任务栈**：

~~~c
static struct fdb_kvdb s_kvdb;
~~~

### 3.2 先注入锁，再初始化

初始化函数：

~~~c
fdb_err_t fdb_kvdb_init(fdb_kvdb_t db, const char *name, const char *path,
                        struct fdb_default_kv *default_kv, void *user_data);
~~~

| 参数 | 值 | 含义 |
| --- | --- | ---- |
| `db` | `&s_kvdb` | 数据库实例 |
| `name` | `"fdb_kvdb"` | 数据库名，可任意起，不要和别的库重名 |
| `path` | `"fdb"` | **FAL 分区名**，必须是 `fal_cfg.h` 分区表里存在的分区 |
| `default_kv` | `NULL` | 默认 KV 表，本工程不用 |
| `user_data` | `NULL` | 用户数据，本工程不用 |

**顺序很关键**：必须先用 `fdb_kvdb_control` 注入锁回调，再调 `fdb_kvdb_init`。因为 init 内部也要走加锁/解锁，锁没注入就 init，多任务环境下可能崩。

### 3.3 初始化的返回值

- 返回 `FDB_NO_ERR`（值为 0）：成功。
- 返回其他值：失败。最常见的失败原因是 `path` 分区在 FAL 分区表里不存在，或分区大小小于两个扇区。

### 3.4 分区大小要求

FlashDB 要求数据库分区至少能放下 **2 个扇区**（本项目 W25Q64 一个扇区 4KB）。`fdb` 分区大小是 512KB，共 128 个扇区，远大于要求。分区表来自 `Middlewares/FAL/port/fal_cfg.h`：

~~~text
fdb   0x00600000   512KB   FlashDB KV 存储
~~~

### 3.5 销毁数据库（可选）

不再使用这个数据库时，可以反初始化，释放其占用的资源：

~~~c
fdb_err_t fdb_kvdb_deinit(fdb_kvdb_t db);
~~~

~~~c
fdb_kvdb_deinit(&s_kvdb);
~~~

本工程 FlashDB 全程常驻、掉电不销毁，所以不需要调用 deinit。

## 4. KV 字符串读写

### 4.1 写入

~~~c
fdb_err_t fdb_kv_set(fdb_kvdb_t db, const char *key, const char *value);
~~~

例子：

~~~c
err = fdb_kv_set(&s_kvdb, "username", "firechip");
~~~

- 键名最长 64 字节（`FDB_KV_NAME_MAX`）。
- 写同一个键就是**覆盖更新**，旧值会变成垃圾，由 FlashDB 的 GC 机制自动回收，不用你管。
- 返回值 `FDB_NO_ERR` 表示写成功。

### 4.2 读取

~~~c
char *fdb_kv_get(fdb_kvdb_t db, const char *key);
~~~

例子：

~~~c
const char *value = fdb_kv_get(&s_kvdb, "username");
if (value != NULL && strcmp(value, "firechip") == 0)
{
    /* 读到的值和写入的一致 */
}
~~~

- 返回 `NULL`：这个键不存在。
- 返回非 `NULL`：指向读取结果的缓冲区，**只读**，不要改写它。

### 4.3 字符串值的长度限制

`fdb_kv_get` 内部使用一块 **128 字节** 的静态缓冲区（`FDB_STR_KV_VALUE_MAX_SIZE`）。也就是说，字符串值超过约 120 字节，`fdb_kv_get` 就读不全了。这时候改用 blob 接口（第 5 节）。

## 5. blob 二进制读写

KV 的值不一定是字符串，也可能是传感器标定数据、校准系数、协议包等二进制数据。这时用 blob 接口。

### 5.1 构造一个 blob

~~~c
fdb_blob_t fdb_blob_make(fdb_blob_t blob, const void *value_buf, size_t buf_len);
~~~

它把 " 一块内存 + 长度 " 打包成一个 `fdb_blob`，不拷贝数据，只是记录指针和长度：

~~~c
struct fdb_blob blob;
uint8_t wbuf[64];

fdb_blob_make(&blob, wbuf, sizeof(wbuf));
~~~

### 5.2 写入 blob

~~~c
fdb_err_t fdb_kv_set_blob(fdb_kvdb_t db, const char *key, fdb_blob_t blob);
~~~

~~~c
err = fdb_kv_set_blob(&s_kvdb, "calib", fdb_blob_make(&blob, wbuf, sizeof(wbuf)));
~~~

### 5.3 读取 blob

~~~c
size_t fdb_kv_get_blob(fdb_kvdb_t db, const char *key, fdb_blob_t blob);
~~~

返回**实际读到的字节数**。用法是 " 先按最大长度构造，再拿返回值判断 "：

~~~c
uint8_t rbuf[64];
size_t rlen;

memset(rbuf, 0, sizeof(rbuf));
rlen = fdb_kv_get_blob(&s_kvdb, "calib", fdb_blob_make(&blob, rbuf, sizeof(rbuf)));

if (rlen == sizeof(wbuf) && memcmp(wbuf, rbuf, sizeof(wbuf)) == 0)
{
    /* 写入和读回完全一致 */
}
~~~

和字符串接口的区别：

| 场景 | 用哪个 |
| ---- | ----- |
| 值是一段短字符串（<128B） | `fdb_kv_set` / `fdb_kv_get` |
| 值是二进制数据 / 长字符串 | `fdb_kv_set_blob` / `fdb_kv_get_blob` |
| 值可能是 `\0` 结尾的 C 字符串 | 用字符串接口更直观 |

## 6. 删除与更新

### 6.1 删除一个键

~~~c
fdb_err_t fdb_kv_del(fdb_kvdb_t db, const char *key);
~~~

~~~c
err = fdb_kv_del(&s_kvdb, "username");
~~~

删除后，这个键对应的 Flash 空间会被标记，之后由 GC 回收。注意：**删除并立即写回大量数据**，可能会触发 GC 擦除扇区，耗时较长，不要放在时间敏感的中断里。

### 6.2 更新一个键

直接再 `fdb_kv_set` 同一个键即可，不用先删：

~~~c
fdb_kv_set(&s_kvdb, "username", "firechip");
fdb_kv_set(&s_kvdb, "username", "new_name");   /* 覆盖为 new_name */
~~~

### 6.3 查看全部键值

调试时可以打印当前数据库里的所有 KV：

~~~c
fdb_kv_print(&s_kvdb);
~~~

## 7. 线程安全与锁

FlashDB 本身不创建线程，它是被你的任务调用的。当**多个任务**可能同时读写同一个数据库时，需要用锁保护。本工程有**两层独立的锁**，理解它们很重要：

| 锁 | 在哪一层 | 保护什么 | 谁创建的 |
| -- | ------- | -------- | ------- |
| 数据库锁 | FlashDB 层（`s_flashdb_mutex`） | 多个任务对同一 DB 的并发操作 | `user_flashdb.c` |
| SPI 总线锁 | SFUD 层（`s_port_mutex`） | 多个中间件对同一物理 SPI 的命令时序 | `drv_adapter_port_externflash.c` |

数据库锁通过 `fdb_kvdb_control` 注入：

~~~c
fdb_kvdb_control(&s_kvdb, FDB_KVDB_CTRL_SET_LOCK,   (void *)flashdb_lock);
fdb_kvdb_control(&s_kvdb, FDB_KVDB_CTRL_SET_UNLOCK, (void *)flashdb_unlock);
~~~

回调里的 `flashdb_lock` 一般就是 " 拿一把互斥锁，永久等待 "：

~~~c
static void flashdb_lock(fdb_db_t db)
{
    (void)db;
    (void)osal_mutex_take(s_flashdb_mutex, OSAL_MAX_DELAY);
}
~~~

底层 SPI 总线锁由 SFUD 在每次读写擦除前自动 take、之后 give，桥接到工程共享的 `s_port_mutex`，应用代码不用管。

如果你只有一个任务碰 FlashDB，不注入锁也能跑；但工程代码始终注入，是为了将来加任务时不踩坑。

## 8. 掉电持久化验证

验证 " 数据真的掉电不丢 "，是本工程的 bring-up 关键步骤：

1. 把 `user_flashdb.c` 顶部的宏改成 0：

~~~c
#define FLASHDB_ERASE_PART_ON_BOOT  0   /* 上电不再擦 fdb 分区 */
~~~

2. 烧录运行一次，写入 `username` 和 `calib`，看到 `FlashDB Test PASS`。
3. **断电，重新上电**（不重新烧录）。
4. RTT 里应看到 KV 直接读回成功，而不是报错——因为数据还在 Flash 里。

默认 `FLASHDB_ERASE_PART_ON_BOOT` 是 1，每次上电都先把 `fdb` 分区擦空，保证测试确定性。真正做持久化验证时记得改成 0。

## 9. TSDB 简介（本工程未启用）

TSDB（时序数据库）适合存 " 带时间戳的传感器数据流 "，按时间顺序追加。本工程只编译了它的源码，没有初始化实例。

如果你要用，大致流程是：

~~~c
/* 初始化：需要一个"取当前时间"的回调 */
fdb_err_t err = fdb_tsdb_init(&s_tsdb, "log", "fdb",
                              tsdb_get_time,  /* 返回 fdb_time_t 的回调 */
                              0, NULL);

/* 追加一条记录 */
fdb_tsl_append(&s_tsdb, fdb_blob_make(&blob, buf, len));
~~~

注意 TSDB 和 KVDB 不能共用同一个 FAL 分区，需要另划分区。当前 `fdb` 分区归 KVDB 用，所以 TSDB 暂时没有可用分区——这就是它只编译不初始化的原因。

## 10. 初学者最容易遇到的问题

| 现象 | 用白话说原因 | 怎么处理 |
| ---- | ----------- | -------- |
| `fdb_kvdb_init` 返回非 0 | `path` 分区在 FAL 分区表里找不到，或分区太小 | 检查 `fal_cfg.h` 分区表是否有 `fdb` 分区，大小是否 >= 2 个扇区 |
| 初始化顺序对、还是报错 | 锁没在 init 之前注入 | 先 `fdb_kvdb_control(SET_LOCK)` 再 `fdb_kvdb_init` |
| 写进去读出来是旧值 | 上电时把分区擦空了，或掉电时序打断了写入 | 持久化验证时把 `FLASHDB_ERASE_PART_ON_BOOT` 改成 0 |
| 读长字符串被截断 | 字符串值超过 128B，`fdb_kv_get` 缓冲区不够 | 改用 `fdb_kv_set_blob` / `fdb_kv_get_blob` |
| 编译过了但任务里卡死 | 在调度器启动前同步做了 FlashDB 读写 | 真实读写必须放在任务里，调度器启动后执行 |
| 频繁删除写入后变慢 | 垃圾多了，GC 在擦扇区 | 属正常现象；避免高频写同一键，可间隔写入 |
| 不确定初始化/读写是否成功 | 没有观测手段 | 用 J-Link mem32 读全局观测变量 `g_flashdb_init_result` 等 |

## 11. 记住这五句话

1. FlashDB 就是帮你把 " 掉电不能丢的数据 " 存进 Flash 的键值库。
2. 标准流程：静态声明 KVDB → 注入锁 → `fdb_kvdb_init` → `fdb_kv_set/get`。
3. 短字符串用 `fdb_kv_get`，二进制和长数据用 `fdb_kv_set_blob`。
4. 所有真实 Flash 读写都要在任务上下文、调度器启动之后进行。
5. 验证掉电持久化，把 `FLASHDB_ERASE_PART_ON_BOOT` 改成 0，断电重启读回。

## 12. 继续学习

- [FlashDB 官方仓库](https://github.com/armink/FlashDB)
- [FlashDB 官方文档（zh_CN）](https://armink.github.io/FlashDB/#/zh-cn/)
- [FlashDB KVDB 使用说明（zh_CN）](https://armink.github.io/FlashDB/#/zh-cn/zh-cn-kvdb)
- [FlashDB TSDB 使用说明（zh_CN）](https://armink.github.io/FlashDB/#/zh-cn/zh-cn-tsdb)

如果某个 API、错误码或 RTT 日志看不懂，可以把具体代码和输出贴出来，按 " 初始化 → 写入 → 读回 → 校验 " 的顺序一起分析。

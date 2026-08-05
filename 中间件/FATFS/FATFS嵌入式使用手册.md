---
title: FATFS 嵌入式使用手册
date: 2026-08-05
tags:
  - 嵌入式
  - 中间件
  - FATFS
  - STM32
  - FreeRTOS
  - Flash
  - W25Q64
aliases:
  - FATFS 使用方法
---

# FATFS 嵌入式使用手册

> [!summary] 适合谁阅读
> 这是一份给第一次接触 FATFS 的嵌入式开发者的入门手册。你不需要先学会 FAL、SFUD 或复杂的分区管理，先照着最小示例把文件写进 Flash 再读出来即可。

本手册讲"移植完成以后怎么用 FATFS"。如果你还没有把 FATFS 放进工程，请先看：

[[FATFS嵌入式移植指南]]

## 1. FATFS 到底是什么

FATFS 是一个跑在 Flash 上的"文件系统"。它帮我们解决一个问题：

> MCU 掉电以后，我要把数据按"文件"的方式保存下来，下次开机还能打开。

如果你用过 FlashDB，可以这样对比：

| 工具 | 存什么 | 数据长什么样 | 适合 |
| ---- | ----- | ----------- | ---- |
| FlashDB（KVDB） | 一个键对应一个值 | `"username" -> "firechip"` | 少量配置、标定数据 |
| FATFS（文件系统） | 命名的文件 + 目录 | `0:test.txt`、`0:/log/xxx.bin` | 日志文件、资源文件、批量数据 |

FATFS 把外置 W25Q64 上的一块区域（本工程是 `fatfs` 分区，1MB）组织成"文件夹 + 文件"的结构，和你电脑上的 C 盘、D 盘是同一套概念。你可以：

- 在根目录写一个 `test.txt`
- 建一个 `log` 文件夹，往里写日志
- 读回、删除、改名

## 2. 先看一次完整测试

本工程在 `User_Task/User_Fatfs/Src/user_fatfs.c` 里有一个现成的文件系统测试线程，是理解 FATFS 用法的最佳入口。它做了四件事：挂载、格式化（仅首次）、写文件、读回校验。

~~~c
#include "ff.h"

static FATFS s_fs;          /* 文件系统对象，静态分配 */
static FIL   s_fil;         /* 文件对象，静态分配 */
static uint8_t s_work[8192];/* f_mkfs 工作区，静态分配 */
static uint8_t s_wbuf[4096], s_rbuf[4096];

void fatfs_test_entry(void)
{
    FRESULT fr;
    MKFS_PARM opt;
    UINT bw = 0, br = 0;

    /* 1. 挂载卷 "0:"（opt=1 表示立即访问磁盘） */
    fr = f_mount(&s_fs, "0:", 1);
    if (FR_NO_FILESYSTEM == fr)          /* 首次使用：还没格式化 */
    {
        memset(&opt, 0, sizeof(opt));
        opt.fmt = FM_ANY | FM_SFD;       /* 自动选型，无 MBR */
        opt.n_root = 128;
        opt.au_size = 4096;              /* 1 扇区/簇 */
        fr = f_mkfs("0:", &opt, s_work, sizeof(s_work));
        fr = f_mount(&s_fs, "0:", 1);    /* 格式化后重新挂载 */
    }
    /* fr == FR_OK 才算挂载成功 */

    /* 2. 写文件：打开（不存在则新建）→ 写入 → 关闭 */
    fr = f_open(&s_fil, "0:test.txt", FA_CREATE_ALWAYS | FA_WRITE);
    fr = f_write(&s_fil, s_wbuf, sizeof(s_wbuf), &bw);
    fr = f_close(&s_fil);
    /* bw == sizeof(s_wbuf) 才算写完整 */

    /* 3. 读回校验：打开 → 读取 → 关闭 → memcmp */
    fr = f_open(&s_fil, "0:test.txt", FA_READ);
    fr = f_read(&s_fil, s_rbuf, sizeof(s_rbuf), &br);
    fr = f_close(&s_fil);
    /* br == sizeof(s_rbuf) 且 memcmp(s_wbuf, s_rbuf) == 0 才算通过 */
}
~~~

这段代码分成四步：

| 代码 | 白话解释 |
| --- | ------- |
| `f_mount(&s_fs, "0:", 1)` | 把"卷 0"挂载到文件系统对象，准备操作这块磁盘 |
| `f_mkfs("0:", &opt, ...)` | 首次使用把磁盘格式化成 FAT 文件系统（类似电脑"格式化"） |
| `f_open` + `f_write` + `f_close` | 新建/打开 `test.txt`，写入数据，关闭 |
| `f_open` + `f_read` + `f_close` | 重新打开，读回数据，和写入的内容比对 |

在 J-Link RTT Viewer 里会看到这样的输出（首次上电含格式化路径）：

~~~text
I/FATFS           [0.150] === FatFs Test ===
I/FATFS           [0.150] no filesystem, f_mkfs FAT12 ...
I/FATFS           [0.302] f_mkfs OK
I/FATFS           [0.304] f_mount OK
I/FATFS           [0.304] f_write OK (len=4096)
I/FATFS           [0.306] write/read verify OK (len=4096)
I/FATFS           [0.306] volume: total=1036288 B, free=1032192 B, free_clst=252
I/FATFS           [0.306] === FatFs Test PASS ===
~~~

## 3. 挂载（f_mount）

每次想用文件系统，第一步都是挂载。可以理解成"告诉 FATFS 这块磁盘从今天开始归我管"。

~~~c
FRESULT f_mount(FATFS *fs, const TCHAR *path, BYTE opt);
~~~

| 参数 | 本工程的值 | 含义 |
| ---- | --------- | ---- |
| `fs` | `&s_fs` | 文件系统对象，**静态分配**，别放任务栈 |
| `path` | `"0:"` | 卷号。本工程只有一个卷，固定是 `"0:"` |
| `opt` | `1` | `1` = 立即访问磁盘读卷信息；`0` = 只登记，不访问 |

注意两点：

- 返回 `FR_OK` 表示挂载成功。
- 返回 `FR_NO_FILESYSTEM` 表示**这块磁盘从来没格式化过**，先去第 4 节格式化。

## 4. 格式化（f_mkfs）

新分区第一次使用必须先格式化，否则 `f_mount` 永远返回 `FR_NO_FILESYSTEM`。

~~~c
FRESULT f_mkfs(const TCHAR *path, const MKFS_PARM *opt, void *work, UINT len);
~~~

格式化参数 `MKFS_PARM` 里的几个关键字段：

| 字段 | 本工程的值 | 含义 |
| ---- | --------- | ---- |
| `fmt` | `FM_ANY \| FM_SFD` | `FM_ANY` = 自动选 FAT12/16/32；`FM_SFD` = 超软盘格式，卷直接建在分区上，不写 MBR 分区表 |
| `n_root` | `128` | 根目录能放多少条目录项（FAT12 下 128 项 = 1 个扇区） |
| `au_size` | `4096` | 每簇多少字节 = 1 扇区。本工程 1MB 分区 → 256 簇 → 自动 FAT12 |
| `n_fat` / `align` | `0`（自动） | 交给 FATFS 自己算 |

**工作区必须静态分配**：`f_mkfs` 需要一块至少 `2 × FF_MAX_SS` = 8KB 的缓冲区，`s_work[8192]` 要声明成全局或静态变量，放进任务栈会溢出。

格式化会**清空整个分区**。如果分区里已经有文件，格式化后全部消失。本工程测试线程每次上电都会检查：有文件系统就直接挂载，没有才格式化。

## 5. 文件读写

### 5.1 打开文件（f_open）

~~~c
FRESULT f_open(FIL *fp, const TCHAR *path, BYTE mode);
~~~

`mode` 决定怎么打开，常用组合：

| 模式 | 作用 |
| ---- | ---- |
| `FA_READ` | 只读打开 |
| `FA_WRITE` | 只写打开 |
| `FA_CREATE_ALWAYS` | 不存在就新建；存在就**清空重写**（最常用） |
| `FA_CREATE_NEW` | 只允许新建；文件已存在则报错 |
| `FA_OPEN_ALWAYS` | 不存在就新建；存在就接着打开（不清空） |
| `FA_OPEN_APPEND` | 打开并定位到**文件末尾**，用于追加 |

写文件常用 `FA_CREATE_ALWAYS | FA_WRITE`（重写），读文件用 `FA_READ`：

~~~c
fr = f_open(&s_fil, "0:test.txt", FA_CREATE_ALWAYS | FA_WRITE);
fr = f_open(&s_fil, "0:test.txt", FA_READ);
~~~

### 5.2 写入（f_write）

~~~c
FRESULT f_write(FIL *fp, const void *buff, UINT btw, UINT *bw);
~~~

- `btw`：想写多少字节。
- `bw`（传出）：**实际写了多少字节**。正常应该等于 `btw`。

~~~c
UINT bw = 0;
fr = f_write(&s_fil, s_wbuf, sizeof(s_wbuf), &bw);
if (FR_OK == fr && bw == sizeof(s_wbuf))
{
    /* 全部写完 */
}
~~~

### 5.3 读取（f_read）

~~~c
FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br);
~~~

- `btr`：想读多少字节。
- `br`（传出）：**实际读了多少字节**。读到文件末尾时可能小于 `btr`。

~~~c
UINT br = 0;
fr = f_read(&s_fil, s_rbuf, sizeof(s_rbuf), &br);
if (FR_OK == fr && br == sizeof(s_rbuf))
{
    /* 读满了，可以 memcmp 校验 */
}
~~~

### 5.4 关闭（f_close）

~~~c
FRESULT f_close(FIL *fp);
~~~

**每次写完或读完都要关闭**。关闭会落盘（内部调用 `CTRL_SYNC`），不关闭就断电，最后写的数据可能丢。

### 5.5 返回值 FRESULT

所有 `f_*` 都返回一个 `FRESULT` 枚举，`FR_OK`（0）是成功。几个常见的：

| 返回值 | 含义 |
| ------ | ---- |
| `FR_OK` | 成功 |
| `FR_NO_FILESYSTEM` | 磁盘没格式化（见第 4 节） |
| `FR_EXIST` | 文件已存在（`FA_CREATE_NEW` 时） |
| `FR_NOT_ENOUGH_CORE` | 内存不够（LFN 栈缓冲不足等） |
| `FR_DISK_ERR` | 底层 Flash 读写出错 |

## 6. 容量查询（f_getfree）

想知道磁盘还剩多少空间：

~~~c
FRESULT f_getfree(const TCHAR *path, DWORD *nclst, FATFS **fatfs);
~~~

~~~c
DWORD nclst;
FATFS *pfs;
fr = f_getfree("0:", &nclst, &pfs);
if (FR_OK == fr && pfs != NULL)
{
    uint32_t free_bytes  = nclst * pfs->csize * 4096;                 /* 剩余空间 */
    uint32_t total_bytes = (pfs->n_fatent - 2) * pfs->csize * 4096;   /* 总空间 */
}
~~~

- `nclst`：剩余簇数。
- `pfs->csize`：每簇多少扇区（本工程 = 1）。
- 4096 是本工程的逻辑扇区大小，所以每簇 = `csize × 4096` 字节。

## 7. 线程安全与锁

FATFS 本身不创建线程，它是被你的任务调用的。本工程 `FF_FS_REENTRANT=0`，**FATFS 内部不加锁**，安全性靠两条约定保证：

| 约定 | 说明 |
| ---- | ---- |
| 单任务访问 | 所有 `f_*` 调用只在一个任务里（本工程是 `fatfs_test` 线程）。如果你多个任务都要用文件，需要自己加锁或改 `FF_FS_REENTRANT=1` 并实现 `ff_mutex_*` |
| 底层 SPI 串行化 | SFUD 每次读写擦前自动 take 工程共享的 `s_port_mutex`，与 FAL/FlashDB/ExternFlash 共用一把锁，不会互相抢 SPI 总线 |

**最重要的纪律**：所有真实 Flash 读写必须放在任务线程里、调度器启动之后执行。调度器启动前同步调用 `f_mount`/`f_read`，SFUD 拿锁时会崩。这也是本工程 `user_fatfs_init()` 只建线程、不碰磁盘的原因。

## 8. 4KB 扇区与 FAT12，你需要知道的三件事

本工程不是标准的 512 字节扇区，而用了 **4KB 逻辑扇区**（对齐 W25Q64 的 4KB 擦除粒度）。三件事记住即可：

1. **为什么是 FAT12**：`fatfs` 分区 1MB ÷ 4KB 扇区 = 256 簇，簇数太少，自动选了 FAT12。FAT12 是这三种 FAT 里最古老、最简单的一种，能容纳的单个文件上限约 4MB，对这个 1MB 分区完全够用。
2. **写放大**：因为一次擦除就是 4KB，写入时哪怕只改一个小文件，底层也是"擦 4KB + 写 4KB"。轻量使用无感，频繁小写入会加快 Flash 磨损。
3. **文件时间戳是假的**：本工程 `FF_FS_NORTC=1`，没有接 RTC，所有文件的修改时间都固定是 2026/8/5。需要真实时间戳时要改宏并实现 `get_fattime()`。

## 9. 初学者最容易遇到的问题

| 现象 | 用白话说原因 | 怎么处理 |
| ---- | ----------- | -------- |
| `f_mount` 返回 `FR_NO_FILESYSTEM` | 这块分区从来没格式化过 | 首次使用先 `f_mkfs`（第 4 节） |
| 任务启动前就调 `f_mount` 直接崩 | SFUD 拿锁依赖调度器已运行 | 所有 `f_*` 放任务线程内、调度器启动后 |
| 写进去读出来不对 | 4KB 扇区三处配置不一致，或 Flash 坏了 | 检查 `ffconf.h` / `diskio.c` / `au_size` 是否都是 4096 |
| 上次写的文件没了 | 测试线程每次上电把分区擦掉重测 | 自己的应用任务只挂载不格式化 |
| 突然 `f_mount` 报 `FR_DISK_ERR` | 中途掉电把 FAT 表写坏了 | 重新 `f_mkfs` 分区 |
| 写大文件很慢 | 4KB 擦除粒度 + 写放大 | 一次性写满扇区，避免零碎小写入 |
| RTT 看不到 FATFS 日志 | elog 没初始化 / RTT 没开 / 线程还没轮到 | 先 `app_elog_init()`，开 RTT Viewer；线程启动有 150ms 延迟 |
| 不确定读写是否成功 | 没有观测手段 | 用 J-Link mem32 读 `g_fatfs_init_result` / `g_fatfs_rw_result` |

## 10. 记住这五句话

1. FATFS 就是帮你把数据按"文件/文件夹"存进 Flash 的文件系统。
2. 标准流程：`f_mount` 挂载 → （首次）`f_mkfs` 格式化 → `f_open`/`f_write`/`f_read` → `f_close`。
3. 写文件用 `FA_CREATE_ALWAYS | FA_WRITE`，读文件用 `FA_READ`，用完一定 `f_close`。
4. 所有真实 Flash 读写都要在任务上下文、调度器启动之后进行。
5. 本工程是 4KB 扇区 FAT12、无真实时间戳；容量计算用 `csize × 4096`。

## 11. 继续学习

- [FatFs 官方站点（elm-chan）](http://elm-chan.org/fsw/ff/00index_e.html)
- [FatFs 官方文档（zh_CN，含全部 API 说明）](http://elm-chan.org/fsw/ff/doc/dirs.html)
- [FatFs GitHub 镜像（ChaN/ff）](https://github.com/ChaN/ff)
- [W25Q64JV datasheet](https://www.winbond.com/resource-files/W25Q64JV%20RevJ%2003232021%20Plus.pdf)
- 配套笔记：[[FATFS嵌入式移植指南]]

如果某个 API、错误码或 RTT 日志看不懂，可以把具体代码和输出贴出来，按"挂载 → 格式化 → 打开 → 读写 → 关闭 → 校验"的顺序一起分析。

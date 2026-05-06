日期：2026.3.23

文章标签： #日志打印 #Easylog_RTT

## 1. 学习内容

### 知识点总览

| 序号  | 知识点                  |
| --- | -------------------- |
| 1   | 理解什么是日志              |
| 2   | 理解什么是基于调试接口的通讯 (RTT) |
| 3   | 移植 Eashlog 库和使用      |

### 知识点关联思维导图

![[Pasted image 20260324194144.png]]

---

## 2. 逐点精讲

### 知识点 1：日志

#### 实际意义

1. 调试与排错，提供详细的线索快速定位问题
2. 监控与性能分析，记录程序启动时间和关键事件的运行状态
3. 审计与安全，记录用户的操作
4. 业务流程记录与分析，记录业务流程执行情况

#### 通俗人话解释

日志可以理解为日记，记录了谁在什么时候在什么地方做了什么事，做的怎么样以及做的过程发现了什么问题

### 知识点 2：RTT 的概念和使用

#### 辅助图示

1. 找到所需的库文件 ![[Pasted image 20260323214959.png]]
2. 需要放入工程的文件 （注意库引用）![[Pasted image 20260323210356.png]]
3. 移植第一个问题 （删除注释即可）![[Pasted image 20260323210104.png]]
4. RTT 缓冲区大小配置 ![[Pasted image 20260323212209.png]]
5. 串口重定向修改（还需要添加头文件# include "SEGGER_RTT.h"）![[Pasted image 20260326195425.png]]
6. 设置输出端口和输出文本颜色 ![[Pasted image 20260323215446.png]]
7. RTT 输出浮点数 ![[Pasted image 20260323213632.png]] ![[Pasted image 20260323214412.png]]
8. 查看窗口 ![[Pasted image 20260323215132.png]]
9. RTT 实现机制 ![[Pasted image 20260324130636.png]]

#### 核心逻辑/原理

RTT 的核心在于它在目标芯片的 RAM 中创建了一个称为 **RTT 控制块（RTT Control Block）** 的结构；这个控制块包含：

- 一个 **标识符（ID）**：用于让 J-Link 调试器能快速在芯片内存中定位这个控制块；
- 多个 **通道结构体**：每个通道对应一个缓冲区，用来描述缓冲区地址、大小、读写指针等状态信息；

#### 关键公式/结论

1. RTT 也叫实时传输，控制块分为 ID 和缓冲区
2. 上缓冲区是由 MCU 向 jlink 发送数据，下缓冲区是由 jlink 向 MCU 发送数据
3. RTT 有多条传输通道，每条传输通道都可以独立实现一种功能

### 知识点 3：Easylog 的移植与使用

#### 应用场景

Easylog 是一款超轻量级 (ROM<1.6K, RAM<0.3K)、高性能的 C/C++ 日志库，非常适合对资源敏感的软件项目，例如：IoT 产品、可穿戴插件设备、智能家居等等

#### 辅助图示

1. 需要下载的文件夹 ![[Pasted image 20260324144436.png]]
2. 移植需要添加的 c 文件 ![[Pasted image 20260324144403.png]]
3. 需要注释的宏定义 ![[Pasted image 20260324144727.png]]
4. 初始化 Easylog ![[Pasted image 20260324145350.png]]
5. 定义接口和输出测试 ![[Pasted image 20260324145417.png]]
6. 带 hal_tick 输出 ![[Pasted image 20260324200142.png]]

#### 关键公式/结论

1. 支持用户自定义输出方式（例如：终端、文件、数据库、串口、485、Flash…）
2. 日志内容可包含级别、计时器、线程信息、进程信息等
3. 日志输出被设计为线程安全的方式，并支持**异步输出**及**缓冲输出**模式
4. 支持多种操作系统（[RT-Thread](http://www.rt-thread.org/)、UCOS、Linux、Windows、Nuttx…），也支持裸机平台
5. 日志支持**RAW 格式**（RAW 格式：未经过修复的原始日志），支持**hexdump**
6. 支持按**标签** 、**级别**、**关键词**进行动态过滤
7. 各级别日志支持不同颜色显示

- ---

## 3. 相关资料

### 🎥 视频链接

[STM32log日志输出](https://www.bilibili.com/video/BV1UN4y167eN/?share_source=copy_web&vd_source=15bad2bcd085cfc0439f4c8d50ecb9b5)

### 🔗 资料链接

[RTT实现日志打印](https://blog.csdn.net/u011493046/article/details/129805728)

[SEGGER_RTT](https://blog.orangetime.top/2024/10/28/mcu/SEGGER-RTT/)

[Easylog移植官方文档](https://github.com/armink/EasyLogger/blob/master/docs/zh/port/kernel.md)

[EasylogAPI官方文档说明](https://github.com/armink/EasyLogger/blob/master/docs/zh/api/kernel.md)

[Easylog代码文件结构图](https://twd6onxsxva.feishu.cn/docx/YP8pdIwRzol3sUx8Ik6csPLSn3f)

[Easylog源码解析](https://twd6onxsxva.feishu.cn/docx/J97mdvUf4otIqpxsQmCcRkbbnVd)

### 💻 代码/PDF

```
#include "elog.h"
#include <stdio.h>

void app_elog_init(void)
{
	elog_init(); // 初始化eLog 初始化的 EasyLogger 的核心功能, 初始化后才可以使用下面的
	//日志颜色功能是将各个级别日志按照颜色进行区分，默认颜色功能是关闭的
	//true：使能，false：失能
	elog_set_text_color_enabled(true);
	/*设置每个级别的日志输出格式*///输出所有内容
	elog_set_fmt(ELOG_LVL_ASSERT, ELOG_FMT_ALL) ;//输出日志级别信息和日志TAG
	elog_set_fmt(ELOG_LVL_ERROR, ELOG_FMT_LVL | ELOG_FMT_TAG);
	elog_set_fmt(ELOG_LVL_WARN, ELOG_FMT_LVL | ELOG_FMT_TAG) ;
	elog_set_fmt(ELOG_LVL_INFO, ELOG_FMT_LVL | ELOG_FMT_TAG) ;//除了时间、进程信息、线程信息之外，其余全部输出
	elog_set_fmt(ELOG_LVL_DEBUG, ELOG_FMT_ALL & ~(ELOG_FMT_TIME | ELOG_FMT_P_INFO | ELOG_FMT_T_INFO));
	//输出所有内容
	elog_set_fmt(ELOG_LVL_VERBOSE, ELOG_FMT_ALL) ;
	// 启动e log 在初始化完成后，必须调用启动方法，日志才会被输出。
	elog_start();
}

void test_elog(void) {
	/* test log output for all level */
	log_a("this assert");
	log_e("this is error");
	log_w("this is warning");
	log_i("this is info");
	log_d("this is debug");
	log_v("this is verbose");
}
```

---

## 4. Q&A

### Q 1：为什么 RTT 的 log 采集不影响 MCU 运行的实时性？

 A 1: MCU 直接将数据写入数据缓冲区，而 jlink 通过 swd 从数据缓冲区中读取，不需要等待 MCU 写完，MCU 和 jlink 互不干扰

### Q 2：RTT 的 log 输出相较于串口 log 输出好在哪？

A 2:

1. 串口输出 MCU 需要等待串口输出完毕才能工作，在多字节的情况下（bps 115200，一个字节就要 87 us，100 个字节就 8.7 ms 了），在操作系统下中断超过 2 tick 就会导致时基偏移
2. 而 RTT 通过 swd 来输出，不会因此 MCU 工作的同时，输出速率还是串口输出的几个数量级以上，同时 RTT 不占用串口 IO 资源

### Q 3：RTT 的输出原理是什么？为什么能把数据传出来？

A 3:

1. MCU 往 RAM 中 RTT 的环形缓冲区存数据，MCU 通过 RTT 控制块的 ID 来找到对应的缓冲区
2. jlink 通过 DP 将信号转换为 32 调试总线数据通过 DAP 总线到达 APB-AP 控制连接系统总线直接访问内存读取缓冲区数据

### Q 4：什么是日志的异步输出？

A 4: 异步输出表示没有时钟信号的同步的情况下进行的数据输出

CPU 不需要等待日志输出完毕才能做其他的事，CPU 将日志输出在缓冲区中，有专门的组件负责在日志被覆盖前输出即可，提升了 CPU 实时性，同时数据输出过快未处理及时就会导致数据丢失

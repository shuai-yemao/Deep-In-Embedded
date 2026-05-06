日期：2026.3.12

文章标签： #STM32 #串口通信 #TTL

## 1. 学习内容

### 知识点总览

| 序号  | 知识点                         |
| --- | --------------------------- |
| 1   | 理解什么是串口通信                   |
| 2   | 理解什么是 TTL 电平以及为何要 TTL 转 [[USB]] |
| 3   | Printf 重定向                  |

### 知识点关联思维导图

![[Pasted image 20260313133905.png]]

---

## 2. 逐点精讲

### 知识点 1：串口通信

#### 辅助图示

1. 数据包 ![[Pasted image 20260312204142.png]]
2. 不同场景配置 ![[Pasted image 20260312204852.png]]
3. 流控方式 ![[Pasted image 20260312205119.png]]
4. 串口框图 ![[Pasted image 20260312210332.png]]
5. 常见串口问题 ![[Pasted image 20260312211315.png]]

#### 核心逻辑/原理

串口通信的本质就是在一定的时间间隔（由波特率决定）内通过两根数据线（RX, TX）将数据按 ascll 码将数据发送到上位机，还需要共地平衡上下位机的参考电压，无需时钟线来同步（异步通信），而是靠时序来确保数据的准确

#### 关键公式/结论

1. 一般串口发送十位数据，第一位起始位（始终为低电平），第 2~9 位为数据位（7 位对应 ascll 码，八位则为一个字节），第 10 位为停止位（始终为高电平）
2. 校验位，用于检验数据是否出错，分为奇校验（数据位 + 校验位中 1 的个数为奇数为正常），偶校验（1 的个数为偶数为正常），无校验（最常用，协议兼容性强）
3. 流控，用于防止数据溢出发送数据过快（RTS/CTS 硬件流控、XON/XOFF 软件流控），简单场景无需配置
4. 波特率其实就是一秒之内发送多少比特，如 9600 即一秒之内发送了 9600 个二进制位
5. 串口轮询模式是最简单的模式，即==发送数据寄存器将数据传递给发送移位数据寄存器==通过移位将数据发送出去，==接收移位数据寄存器从 RX 接收数据移位到接收数据寄存器==，CPU 从寄存器中读取和存取数据
6. 串口中断解决了轮询模式阻塞 CPU 的问题，发送数据和接收数据时触发中断来呼唤 CPU 控制，结束中断就释放来 CPU 资源，==STM 32 中在串口中断回调函数中将数据移入数据缓冲区中==（方便对接收到的数据解析）并在末尾插入 HAL_UART_Receive_IT 打开下一次串口接收中断（==串口回调函数的 __weak 弱定义表示可以在他处重新定义函数，另起炉灶==）
7. 串口通信常自定义协议封装==数据包==，一般为包头 + 长度 + 数据 + 校验 + 包尾
8. ==串口自定义协议解析的核心逻辑为状态机模式==

### 知识点 2：TLL，RS 232 ，RS 485

#### 核心逻辑/原理

1. 晶体管 - 晶体管逻辑（Transistor-Transistor Logic）简称 TTL，逻辑电平定义为 0~0.4 v 为逻辑 0,2.4 v~5 v 为逻辑 1，因为 TTL 的噪声容限 ( ==指的是电路或逻辑门在确保其输出状态不被误判的前提下，能够承受的最大外来噪声或干扰电压==) 为 0.4 v 缓冲，所以输出低电平为 0~0.4 v，输入低电平为 0~0.8 v
2. RS 232 由 EIA 制定的串行通信接口标准，负逻辑电平，逻辑 1 为负电平，逻辑 0 为正电平，+3~15 v 为逻辑 0，-3~15 v 为逻辑 1，噪声容限为 2 v
3. RS 485 由 EIA 制定的差分串行通信接口协议，逻辑电平定义为 +2< A-B <+6 为逻辑 0，-6 < A-B <-2 为逻辑 1

#### 关键公式/结论

1. TLL 为单端信号传输，易受外界环境干扰，传输距离不能超过一米，适合板上通信，全双工模式
2. RS 232 传输距离在 15 米以内，采用单端信号传输，抗干扰能力一般，传输速率在 10 kb 左右，也是全双工模式
3. RS 485 传输距离达到了 1200 米左右（波特率<100 kbps），抗共模干扰能力强（雷电情况下会拉高两线电平但是不会影响两线之间电压差），采用半双工模式，传输速率达到了 10 M 左右
4. TTL 和 RS 232 都是点对点通信，RS 485 是一主多从，最大支持 32 个从机
5. [[USB]] 转 TTL （[[USB]] 对上位机创建模拟串口来通信）是通过 CH 340 类型芯片将 [[USB]] 协议转换成 TTL 与下位机串口通信, 如转换成 RS 232/RS 485 还需要电平转换芯片

### 知识点 3：printf 重定向

#### 实际意义

重定向是为了统一输出接口和提高移植性

#### 辅助图示

1. 修改后的重定向 ![[Pasted image 20260313122952.png]]
2. 重定向强制控制任务调度方法弊端 ![[Pasted image 20260313123844.png]]

#### 核心逻辑/原理

Printf 函数的重定向是一种将标准输出重定向到其他输出设备或接口的技术，在 STM 323 中通过重新定义标准库 stdio 中 fputc 函数来使用 printf 函数输出串口数据，要勾选 microlib 库才能正常使用

#### 关键公式/结论

1. 在操作系统中，直接重定向会导致通信过程中丢数据
2. 为了防止丢失数据，使用 vTaskSuspendAll （） 强行停止任务调度，等打印完了再使用 xTaskResumeAll （）恢复任务调度，这个方法虽然能防止数据丢失，但是会降低系统的实时性
3. 使用日志系统，互斥量保护，和临时区保护以及环形缓冲区加 DMA 可以解决 printf 数据丢失以及降低系统的实时性的问题

- ---

## 3. 相关资料

### 🎥 视频链接

[超简单的一种通信，2分钟搞懂，串口通讯的工作原理！_哔哩哔哩_bilibili](https://www.bilibili.com/video/BV1y34y147s5/?share_source=copy_web&vd_source=15bad2bcd085cfc0439f4c8d50ecb9b5)

[【STM32入门教程-2025】第9集 STM32串口原理与串口中断模式收发 | keysking的stm32教程_哔哩哔哩_bilibili](https://www.bilibili.com/video/BV1bc411J7Tv/?share_source=copy_web&vd_source=15bad2bcd085cfc0439f4c8d50ecb9b5)

[【干货分享】2分钟快速了解电平通讯TTL、RS232、RS485_哔哩哔哩_bilibili](https://www.bilibili.com/video/BV1pk4y137c9/?share_source=copy_web&vd_source=15bad2bcd085cfc0439f4c8d50ecb9b5)

[快速上手CH340N电路设计（USB转串口模块 原理图和PCB工程 USB转TTL 模块 USB Type-C接口 CH340系列芯片讲解）_哔哩哔哩_bilibili](https://www.bilibili.com/video/BV1LM4y1L7Vg/?share_source=copy_web&vd_source=15bad2bcd085cfc0439f4c8d50ecb9b5)

[51单片机入门学习基础篇—USB-TTL模块的使用总结_哔哩哔哩_bilibili](https://www.bilibili.com/video/BV1Dx4y1w7Qm/?share_source=copy_web&vd_source=15bad2bcd085cfc0439f4c8d50ecb9b5)

[4分钟快速了解串口屏通讯接口类型TTL、RS232和RS485这3种嵌入式经典通信总线协议_哔哩哔哩_bilibili](https://www.bilibili.com/video/BV1CR4y1C7qx?spm_id_from=333.788.recommend_more_video.2&trackid=web_related_0.router-related-2479604-gjmc5.1773321876762.114&vd_source=15bad2bcd085cfc0439f4c8d50ecb9b5)

[STM32单片机printf重定向_哔哩哔哩_bilibili](https://www.bilibili.com/video/BV1GF411U7DZ/?share_source=copy_web&vd_source=3868aa3c06645a03ebfe5a53e5bd5894)

[单片机串口通信不理解？STM32的USART和UART差在哪里？几分钟给你讲清楚！（STM32教程基于HAL库和CUBEIDE）_哔哩哔哩_bilibili](https://www.bilibili.com/video/BV1vZ4y1b7AA/?share_source=copy_web&vd_source=15bad2bcd085cfc0439f4c8d50ecb9b5)

### 🔗 资料链接

[串口通信（UART）完全指南：底层原理 + 协议解析 + 数据可视化 + 调试技巧](https://devresourcehub.com/zh/%e4%b8%b2%e5%8f%a3%e9%80%9a%e4%bf%a1%ef%bc%88uart%ef%bc%89%e5%ae%8c%e5%85%a8%e6%8c%87%e5%8d%97%ef%bc%9a%e5%ba%95%e5%b1%82%e5%8e%9f%e7%90%86-%e5%8d%8f%e8%ae%ae%e8%a7%a3%e6%9e%90-%e6%95%b0%e6%8d%ae.html)

[串口通信全面详解_串口通信详解-CSDN博客](https://blog.csdn.net/qq_44647100/article/details/155641545)

[TTL电平协议、RS232电平协议、RS485电平协议、 - 记得要好好吃饭 - 博客园](https://www.cnblogs.com/eatlong/p/18969179)

[单片机通信协议--USART(串口通信) - 详解 - ljbguanli - 博客园](https://www.cnblogs.com/ljbguanli/p/19325229)

[《波特率和比特率的区别解析》（建议收藏！！！）-腾讯云开发者社区-腾讯云](https://cloud.tencent.com/developer/article/2595390)

[【单片机】深入剖析USART与UART的区别_uart和usart有什么区别-CSDN博客](https://blog.csdn.net/W__winter/article/details/143221158)

[为什么printf需要勾选microlib?](https://shatang.github.io/2020/05/30/%E5%BE%AE%E5%BA%93-%E6%96%AD%E8%A8%80-Keil-%E4%BB%A3%E7%A0%81%E4%BC%98%E5%8C%96/)

[strcpy、strncpy和memcpy的用法与区别详解_memcpy和strncpy-CSDN博客](https://blog.csdn.net/century_sunshine/article/details/80738139)

[【C语言内功心法】__weak -- 示弱也是一种强大_c语言 weak-CSDN博客](https://blog.csdn.net/m0_37697335/article/details/81628417)

[(99+ 封私信 / 2 条消息) TTL和CMOS，你会分辨吗？ - 知乎](https://zhuanlan.zhihu.com/p/662084172)

[(99+ 封私信 / 2 条消息) 什么是噪声容限 - 知乎](https://zhuanlan.zhihu.com/p/1971312848241157091)

### 代码/PDF

```
#ifdef __GNUC__
	#define PUTCHAR_PROTOTYPE int _io_putchar(int ch)
#else 
	#define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *F)
#endif
	
//PUTCHAR_PROTOTYPE //usart printf
//{
////		vTaskSuspendAll();
//		HAL_UART_Transmit(&huart1,(uint8_t *)&ch,1,0xFFFF);
////		xTaskResumeAll();
//		return ch;
//}

PUTCHAR_PROTOTYPE //RTT printf
{
	SEGGER_RTT_PutChar(0,ch);
	return ch;
}
```

---

## 4. Q&A

### Q 1：UART 和 USART 有何区别？

 A 1:

 1. [[UART]] 仅支持异步通信（无时钟同步信号），适合中低速通信
 2. USART 可以同步通信也可以异步通信，因为可以通过时钟线同步通信，可以实现更高的通信速率且误码率低，在高速通信中也是不错的选择

### Q 2：什么是波特率？波特率和比特率的区别？

A 2:

1. 波特率指单位时间内传输的码元个数（单位波特 Baud），码元是承载信号量的基本信号单位，在不同进制的情况下，携带的信息位数不同，二进制中码元携带一位信息，八进制中码元携带三位数据
2. 比特率是指单位时间内传输的二进制位数（单位 bps），在二进制中波特率和比特率数值上相等
3. 其中总码元数 Nm = Rb (波特) x t (传输时间) ，总比特数 Nb = n （码元携带的信息位数）x Nm，总字节数 Nz = Nb / 8

### Q 3：操作系统下串口通信有数据丢失问题（线程安全问题），原因是什么？如何解决？

A 3: 其原因是 rtos 的时间片调度在任务已经输出部分字符时转换任务导致数据丢失，其解决办法可以在输出数据前使用 vTaskSupsend 函数停止任务调度等数据发送完成再打开任务调度即可但是会影响系统的实时性，一般是在重定向函数前加入任务调度停止函数而不是在任务中

### Q 4：为什么时钟配置时不使用系统滴答定时器，而是选择一个定时器作为系统时钟

A 4: systick 是 arm 内核层面的特殊定时器 (SysTick 是一  个 24 位的倒计数定时器)，其主要是为操作系统提供统一的时钟信号，在 ==freertos 操作系统中为了保证系统的实时性和外部中断的优先级高于系统调度 systick 的优先级通常被设置成最低==，在中断中调用依赖 HAL 时基会因为其优先级过低无法及时响应导致系统卡死，所以需要另外选择一个定时器提高其优先级使 HAL 函数不会因为优先级限制保证系统的稳定性和实时性

### Q 5：什么是 printf 重定向？为什么要重定向？

A 5: 重定向就是通过重新定义标准库 stdio 中 fputc 函数来使用 printf 函数输出串口数据，重定向可以统一输出接口和节省系统资源以及支持格式化输出

### Q 6：串口通信的 vcc 和 gnd 是否一定要接？为什么？

A 6: gnd 是一定要接的，接收和发送两方的参考电压不一致会让信号的高低电平无法被对方识别从而导致通信失败，vcc 可以不接

### Q 7：串口通信中为什么使用 strncpy 和 memcpy 这类函数？

A 7: 防止数据缓冲区溢出，strncpy 和 memcpy 会对控制复制字符长度来防止数据溢出

### Q 8：串口是否有起始位？如何有是怎样的？

A 8: 串口一般有一位起始位，始终为低电平

### Q 9：什么是 git stash？什么时候需要使用 git stash 和 git stash pop？

A 9:

1. 当遇到紧急开发任务，但是当前任务还没开发完，不能提交时，会先执行 git stash,然后进行紧急任务的开发，然后通过 git stash pop 取出暂存区的内容进行继续开发；
2. 当切换分支时，但是当前分支已经修改的内容还不能提交时，会先执行 git stash 再进行分支切换，等回来当前分支进行 git stash pop 取出。

### Q 10：TTL 和 CMOS 有何区别？

A 10:

1. TTL 电平：电压范围在 0～5V，常见都是 5V。5V TTL 中输入大于 2V 算高电平，输入小于 0.8V 算低电平；
2. CMOS 电平：电压范围在 3～15V；常见电压在 12V。5V CMOS 中输入大于 3.5V 算高电平，输入小于 1.5V 算低电平

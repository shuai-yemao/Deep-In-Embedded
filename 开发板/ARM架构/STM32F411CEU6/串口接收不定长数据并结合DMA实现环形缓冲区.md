日期：20263.31

文章标签： #串口通信 #DMA #环形缓冲区

## 1. 学习内容

### 知识点总览

| 序号  | 知识点         |
| --- | ----------- |
| 1   | 串口如何接收不定长数据 |
| 2   | 串口结合 DMA 工作 |

### 知识点关联思维导图

![[assets/串口接收不定长数据并结合DMA实现环形缓冲区/file-20260421201607441.png]]

---

## 2. 逐点精讲

### 知识点 1：串口接收不定长数据

#### 核心逻辑/原理

串口接收不定长数据的重要点是记录接收到的数据长度和串口数据转移至缓冲区

#### 关键公式/结论

1. 使用串口接收中断利用状态机思想来自定义协议来接收不定长数据（数据量少时可以使用，传输速率高易中断时间过长，可以将解包放入线程中来减少运算量）
2. ==使用串口空闲中断记录接收数据长度加 DMA 接收数据来接收不定长数据==（也可以使用串口接收中断来接收数据到缓冲区中）
3. 利用定时器中断来实现超时判断（与空闲中断原理类似），使用串口接收中断来接收数据到缓冲区中，超时判断的时间跟波特率有关，超时时间最小可以设置为 1.5 倍的单字节接收时间
4. ==Idle [[中断]]，当串口的起始位在持续 1 个字节的时间都没有到来的时候，触发空闲中断==
5. [[UART]] 的空闲中断回调函数 HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size))
6. 空闲中断触发原理：串口外设在个字节的时间中没有接收到串口起始位，则发生中断 IDLEI。IDLEIE（空闲中断使能寄存器）

### 知识点 2：串口结合 DMA 工作

#### 实际意义

由于在高速传输下，串口会因为快速请求中断，而导致在悬起位置一时，其他中断被丢失，从而丢失数据，而环形缓冲区加 DMA 以及空闲中断的组合大大减少了数据的丢包率，DMA 的参与解放 CPU，而空闲中断以及 DMA 半满全满极大的减少了中断申请量，让 CPU 不会被重复打断，环形缓冲区的作用是大幅提高了内存利用率

#### 辅助图示

1. 串口框图 ![[assets/串口接收不定长数据并结合DMA实现环形缓冲区/file-20260421201607411.png]]
2. 串口发送器时序图 ![[assets/串口接收不定长数据并结合DMA实现环形缓冲区/file-20260421201607420.png]]
3. 串口接收器时序图 ![[assets/串口接收不定长数据并结合DMA实现环形缓冲区/file-20260422211237440.png]]
4. 串口中断请求 ![[assets/串口接收不定长数据并结合DMA实现环形缓冲区/file-20260421201607414.png]]
5. 串口中断映射图 ![[assets/串口接收不定长数据并结合DMA实现环形缓冲区/file-20260421201607417.png]]
6. 环形缓冲区加空闲中断以及 DMA 半满和全满中断思路 ![[assets/串口接收不定长数据并结合DMA实现环形缓冲区/file-20260421201607445.png]]
7. 串口驱动程序设计思路 ![[assets/串口接收不定长数据并结合DMA实现环形缓冲区/file-20260421201607422.png]]
8. 动态偏移算法思路 ![[assets/串口接收不定长数据并结合DMA实现环形缓冲区/file-20260421201607430.png]]
9. DMA 全满中断 ![[assets/串口接收不定长数据并结合DMA实现环形缓冲区/file-20260421201607435.png]]10 .串口空闲中断 ![[assets/串口接收不定长数据并结合DMA实现环形缓冲区/file-20260421201607438.png]]

#### 核心逻辑/原理

假设此次传输 11 个字节，环形缓冲区大小为 10，在传输 4,9 个字节后分别进入 DMA 半满和全满中断，在回调函数中更新队头位置，并通知线程 A，将数据通过线程 B 将缓冲区数据取出，而在 11 个字节传输后，传输一个字节时间内未有数据传入，触发空闲中断，更新队头位置，通知线程 A，而大于环形缓冲区大小的字节，由 DMA 模式决定，circle 则覆盖第一个字节, 数据接收结束，由线程 B 状态机判断是否已结束，已结束则开始计算检验和，传输和接收数据无遗漏，则通过 RTT 输出缓冲区数据

#### 关键公式/结论

1. 环形缓冲区主要解决不定数据接收到的问题，这是和 A-BBuffer 最大的区别，A-BBuffer 是接收固定长度的，==对于不定长数据，环形缓冲区对空间利率较高，MCU 使用更少的 RAM==。
2. 中断一（DMA 半满中断），中断二（DMA 全满中断），中断三（串口空闲中断）之间的关系：
	1. 当使用 HAL_UARTEx_ReceiveToIdle_DMA 函数时，假设要接收的数据是 32 字节（半满 16 字节）当发送 17 个字节时，会先进入中断一（DMA 半满中断），然后再进入中断三（串口空闲中断）
	2. 当发送 15 个字节时，会直接进入中断三（串口空闲中断）。
	3. 当发送 33 个字节时，会先进入中断一（DMA 半满中断），然后进入中断二（DMA 全满中断），然后进入中断三（串口空闲中断）。
	4. 并且，由于时 33 个字节，如果这个时候 DMA 的配置为 DMA_NORMAL 模式，则第 33 个字节会被丢掉。如果，==DMA 被配置为 DMA_CIRCULAR，则第 33 个字节会覆盖最开始的第一个字节。==
3. ==HAL_UART_RxCpIltCallback，主要用于固定长度的 [[UART]] 接收==，当接收到设定长度的数据后触发。用 HAL_UART_Receive_IT 或 HAL_UART_Receive_DMA 函数，接收到指定长度的时候会调用，不适用于接收可变长的串口数据。
4. ==HAL_UARTEx_RxEventCallback，主要用于可变长度的数据接收，特别是结合空闲行检测==，可以在接收到不确定长度的数据时触发。通常与 DMA 接收模式一起用，主要用于 HAL_UARTEx_ReceiveToIdle_DMA 函数，当检测到空闲或者到达 DMA 最大缓冲时，使用这个回调。
5. 所有的采样设备其本质都是 [[ADC]] 采集
6. 串口数据寄存器 DR 有数值时，CR 1 寄存器 RXNE 置一，==hal 库未开启串口接收中断初始化会自动将 RXNE 复位，开启后也只能将 RXNE 置一一次，所以中断回调最后都会重新开启串口接收中断初始化==
7. 发送两个字节会导致系统卡死的原因？串口接收函数因为 log 串口发送函数使中断时间较长，从而使中断悬起位挂起后被软件消除导致丢中断
8. ==环形缓冲区初始化填充要为零，为 0 xff 的话读取初始队头为 -1==
9. 环形缓冲区存或取数时，要先取余后加队头或队尾，先加后取余会导致队尾到设定环形缓冲区个数即清零，使用动态偏移算法无法与队头同步导致重复发送串口数据
10. 环形缓冲区判空方法
	1. 计数，每次存取和读取都计数
	2. 空一个数据，队尾下一个是队头，则满

- ---

## 3. 相关资料

### 🎥 视频链接

[freertos下串口驱动架构](https://www.bilibili.com/video/BV1xf421D7aX/?spm_id_from=333.999.0.0&vd_source=8599f11aa9cca17e6373aac78baf1844)

[串口状态机和环形缓冲区](https://www.bilibili.com/video/BV1g84y1s7m1/?spm_id_from=333.337.search-card.all.click&vd_source=8599f11aa9cca17e6373aac78baf1844)

### 🔗 资料链接

[串口接收不定长数据的方法](https://zhuanlan.zhihu.com/p/566271584)

### 💻 代码/PDF

```
//串口结构体和宏定义
#define  RX_MAXLEN  200  //最大接收数据长度

typedef struct{
      uint8_t  RxBuf[RX_MAXLEN];//接收缓存
      uint16_t RxCnt;    //接收数据计数
      uint16_t RxLen;    //接收数据长度
      uint8_t RxStart;    //开始接收标志
      uint8_t RxFlag;    //一帧数据接收完成标志
}Uart_Tpye_t;

Uart_Tpye_t Uart1;

//打开串口空闲中断和DMA接收
__HAL_UART_ENABLE_IT(&huart1,UART_IT_IDLE);//打开串口空闲中断 
HAL_UART_Receive_DMA(&huart1, Uart1.RxBuf, RX_MAXLEN); //串口DMA接收数据

//打开串口接收中断
HAL_UART_Receive_IT(&huart1, &RevByte, 1); //串口中断接收数据

//串口空闲中断
void UART_IDLECallBack(UART_HandleTypeDef *huart)
{
    uint32_t temp;  
  /*uart1 idle processing function*/
    if(huart == &huart1)
    {
      if((__HAL_UART_GET_FLAG(huart,UART_FLAG_IDLE) != RESET))  
      {
        __HAL_UART_CLEAR_IDLEFLAG(&huart1);//清除标志位
        /*your own code*/
        HAL_UART_DMAStop(&huart1);//停止DMA
        Uart1.RxLen = RX_MAXLEN - __HAL_DMA_GET_COUNTER(&hdma_usart1_rx);// 获取DMA中传输的数据个数
        Uart1.RxFlag = 1;
        HAL_UART_Receive_DMA(&huart1,Uart1.RxBuf,RX_MAXLEN); //开启下次接收
      }
    }
}

//串口接收中断
uint8_t RevByte;
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{    
  if(huart->Instance==USART1)
  {
    Uart1.RxBuf[Uart1.RxCnt]=RevByte;
    Uart1.RxCnt++;
    if(Uart1.RxCnt==RX_MAXLEN)
    {
      Uart1.RxCnt = RX_MAXLEN-1;
    }
    HAL_UART_Receive_IT(&huart1, &RevByte, 1); //串口中断接收数据
  }
}

//状态机思想
uint8_t RevByte;
uint16_t RevTick = 0;
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  static uint16_t Rx_len;

  if(huart->Instance==USART1)
  {
    Uart1.RxBuf[Uart1.RxCnt]=RevByte;
    switch(Uart1.RxCnt)
    {
      case 0:
        if(Uart1.RxBuf[Uart1.RxCnt] == 0x5A)//帧头1正确
          Uart1.RxCnt++;
        else
          Uart1.RxCnt = 0;
        break;
      case 1:
        if(Uart1.RxBuf[Uart1.RxCnt] == 0xA5)//帧头2正确
          Uart1.RxCnt++;
        else
          Uart1.RxCnt = 0;
        break;
      case 2:
        Rx_len = Uart1.RxBuf[Uart1.RxCnt];
        Uart1.RxCnt++;
        break;
      default:
        Uart1.RxCnt++;
        if((Rx_len+3) == Uart1.RxCnt)//数据接收完成
        {
          Uart1.RxFlag = 1;
          Uart1.RxLen = Uart1.RxCnt;
          Uart1.RxCnt = 0;
        }
        break;
    }
    HAL_UART_Receive_IT(&huart1, &RevByte, 1); //串口中断接收数据
  }
}

//串口接收超时判断，该函数在Systick中断（1ms中断一次）中调用
void UartTimeOut()
{
  if(Uart1.RxStart == 1)
  {
    RevTick++;
    if(RevTick > 2)
    {
      Uart1.RxLen = Uart1.RxCnt;
      Uart1.RxCnt = 0;
      Uart1.RxStart = 0;
      Uart1.RxFlag = 1;
    }
  }
}
```

```
//同时打开空闲中断和DMA半满和全满中断
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *huart, uint8_t *pData, uint16_tSize)；（胶水接口DMA+UART）

//创建环形缓冲区
circle_buffer_t * buffer1 = createEmptyCirclrBuffer();
data_type_t data1 = 2;

//环形环形区测试代码
if(NULL == buffer1)
{
	log_e("buffer create failed\r\n");
}
if(0x00 == buffer_is_empty(buffer1))
{
	log_i("buffer is empty\r\n");
}
if(0x00 == buffer_is_full(buffer1))
{
	log_i("buffer is full\r\n");
}
if(0x01 == insert_data_to_buffer(buffer1,&data1))
{
	log_i("buffer data = %d\r\n",data1);
}
if(0x01 == get_data_from_buffer(buffer1,&data1))
{
	log_i("buffer data = %d\r\n",data1);
}
```

---

## 4. Q&A

### Q 1: 为了减少进中断的次数，使用的全满中断 + 半满 + 空闲，为什么不直接使空闲 + 全满？这样进中断的次数不是更少？

 A 1: 实际上全满中断只在 DMA 缓存填满时触发，空闲中断依赖串线路出现空闲时间（帧间隙）才会触发，==如果遇到速连续数据流，比如多个数据帧尾相接、中间没有明显间隔，这种情况下空闲中断可能不会触发，数据没填满，就可能导致数据被覆盖、出现丢包==。加半满中断可以在 DMA 接收到半数据时提前处理，减轻缓存压力，降低丢包概率。

### Q 2：为什么串口需要环形缓冲区？

A 2: 环形缓冲区相比于 AB 双缓冲区，其空间利用率更高，只需要一个缓冲区 A 甚至更少即可实现双缓冲区的功能，对于不定长数据也不会因为缓冲区大小原因导致数据丢失

### Q 3：串口的单次最大接收数据量是否需要限制？

A 3: 需要限制，根据缓冲区大小来设置，接收量过大会导致线程无法及时处理数据从而导致丢失数据，同时会影响系统其他线程运行

### Q 4：是否可以根据每次数据量大小来每次接收时临时 malloc 内存？如何想要通过 malloc 来实现，应该怎么判断每次分配多少大小空间的内存？

A 4:

1. 可以在线程中分配内存空间，中断中不建议，分配时间过长会影响系统时基，同时中断次数过多，内存分配未释放会导致堆溢出，同时分配过多会导致内存碎片化易导致数据丢失
2. 在数据包中加入长度标记，解包后根据标记分配内存即可

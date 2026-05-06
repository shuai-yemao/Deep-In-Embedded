---
project: 药箱-YaoXiang
mcu: STM32F103C8T6
peripheral: USART1+USART2, DMA1_Channel5/6, ESP8266
created: 2026-05-05
status: 已解决
---

# USART2 DMA接收缓冲区全0x00——IDLE标志预设 + TX硬件故障

## 一、问题描述

ESP8266初始化过程通过USART2发送AT指令，DMA+IDLE中断能正常进入回调(`HAL_UARTEx_RxEventCallback`)，RX_STA显示 `0x8003`（就绪标志=1，长度=3），但接收缓冲区 `RX_BUF[0..2]` 全部为 `0x00`，并未收到ESP8266返回的 `OK`。同时，上位机通过USART1观察调试输出为空白。将ESP8266从USART2(PA2/PA3)改接到USART1(PB6/PB7)后通信恢复正常，确认**USART2 TX引脚硬件故障**。

## 二、原因分析

### 根因1：USART_SR.IDLE 上电默认置位导致虚Size=0回调

STM32F103 USART外设使能后，`USART_SR.IDLE` 标志硬件默认为 **1**（因RX线空闲）。`UART_Stable_Init()` 中直接调用 `HAL_UARTEx_ReceiveToIdle_DMA()` 时HAL检测到IDLE已置位，立即触发 `RxEventCallback(Size=0)`：

```
RX_STA = RX_STA_READY | (0 & RX_STA_LEN_MSK) = 0x8000 | 0 = 0x8000
Serial_GetLen() = 0x8000 & 0x3FFF = 0
```

导致 `wait_for()` 轮询始终读到长度=0，3次retry超时，`ESP8266_Init` 返回 `ESP_FAIL`。

### 根因2：USART2 TX引脚硬件故障导致ESP8266从未收到指令

`HAL_UART_Transmit(&huart2, "AT\r\n", ...)` 阻塞发送未报错（函数正常返回HAL_OK），但**PA2(USART2_TX)引脚实际输出全为0x00**（Break条件/UART帧错误）。ESP8266从未收到有效AT指令，因此无任何回复。

RX线（PA3）处于浮空/低电平状态，噪声被UART解析为连续的 `0x00` 帧（FE=1帧错误），DMA捕获后存入缓冲区。

| 诊断信息 | 含义 |
|---------|------|
| `RX_STA = 0x8003` | 3字节帧完成（就绪+长度3） |
| `buf[0..2] = 0x00` | 接收数据为Break/噪声 |
| `USART_SR & 0x01 = 1` (FE) | 帧错误——RX线电平异常 |
| `HAL_UART_Transmit` 返回 `HAL_OK` | TX阻塞发送不检测引脚电平 |

### 根因3：ISR内阻塞UART_Printf导致HAL状态机卡死

在 `HAL_UARTEx_RxEventCallback`（USART2中断上下文）中调用 `UART_Printf(&huart1, ...)`，其中 `HAL_UART_Transmit` 是**阻塞函数**。在ISR优先级5的环境下，USART1 TX轮询可能失败，导致 `huart1.gState` 卡在 `BUSY_TX` 状态，后续所有USART1发送静默返回 `HAL_BUSY`，上位机终端看不到任何输出。

## 三、实验设计

| # | 实验 | 预期 | 实际结果 |
|---|------|------|---------|
| 1 | `HAL_UARTEx_ReceiveToIdle_DMA`前加 `__HAL_UART_CLEAR_IDLEFLAG` | Size不再为0 | 通过：Size从0变为实际接收字节数 |
| 2 | 回调中打印 `USART_SR` 和 `RX_BUF[0..2]` | 判别FE标志和实际数据 | 通过：SR显示FE=1 + buf全0x00 |
| 3 | ESP8266从USART2改接USART1 | 通信正常 | 通过：确认USART2 TX硬件故障 |
| 4 | ISR中阻塞打印改为标记位+任务消费 | 上位机恢复输出 | 通过：上位机可收到调试信息 |

## 四、验证实验

1. **修复1验证**：编译烧录后 `__HAL_UART_CLEAR_IDLEFLAG` 使IDLE回调Size正确（从0变为实际值）
2. **修复2验证**：`Serial_DebugPrint()` 在 `wait_for()` 任务轮询中消费诊断标记，USART1终端可看到诊断信息
3. **换口验证**：`mqtt_cfg.huart = &huart1`，ESP8266连PB6/PB7，`ESP8266_Init` 成功，MQTT发布正常
4. **移除ISR阻塞发送后**：`huart1.gState` 恢复 `READY`，所有 `UART_Printf` 正常输出

## 经验教训

1. **HAL_UARTEx_ReceiveToIdle_DMA 首次调用前必须清IDLE**：`__HAL_UART_CLEAR_IDLEFLAG(huart);`
2. **ISR中严禁调用阻塞HAL函数**：`HAL_UART_Transmit`（含`HAL_MAX_DELAY`）必须在任务/主循环上下文使用
3. **TX正常返回不等于引脚正常输出**：HAL阻塞发送只检查寄存器标志（TXE/TC），不检测引脚电平
4. **DMA接收全0x00第一步查SR寄存器**：`USART_SR.FE=1` 说明物理层异常，优先排查引脚/接线
5. **USART DMA诊断三板斧**：1.清IDLE 2.读SR(FE/ORE/NE) 3.ISR只置标记不阻塞

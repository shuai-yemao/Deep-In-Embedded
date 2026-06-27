---
project: 药箱-YaoXiang
mcu: STM32F103C8T6
peripheral: USART1+USART2, DMA1_Channel5/6, ESP8266
created: 2026-05-05
status: 已解决
---

# USART2 DMA 接收缓冲区全 0x00——IDLE 标志预设 + TX 硬件故障

## 一、问题描述

ESP8266 初始化过程通过 USART2 发送 AT 指令，DMA+IDLE 中断能正常进入回调 (`HAL_UARTEx_RxEventCallback`)，RX_STA 显示 `0x8003`（就绪标志=1，长度=3），但接收缓冲区 `RX_BUF[0..2]` 全部为 `0x00`，并未收到 ESP8266 返回的 `OK`。同时，上位机通过 USART1 观察调试输出为空白。将 ESP8266 从 USART2(PA2/PA3) 改接到 USART1(PB6/PB7) 后通信恢复正常，确认**USART2 TX 引脚硬件故障**。

## 二、原因分析

### 根因 1：USART_SR.IDLE 上电默认置位导致虚 Size=0 回调

STM32F103 USART 外设使能后，`USART_SR.IDLE` 标志硬件默认为 **1**（因 RX 线空闲）。`UART_Stable_Init()` 中直接调用 `HAL_UARTEx_ReceiveToIdle_DMA()` 时 HAL 检测到 IDLE 已置位，立即触发 `RxEventCallback(Size=0)`：

```
RX_STA = RX_STA_READY | (0 & RX_STA_LEN_MSK) = 0x8000 | 0 = 0x8000
Serial_GetLen() = 0x8000 & 0x3FFF = 0
```

导致 `wait_for()` 轮询始终读到长度=0，3 次 retry 超时，`ESP8266_Init` 返回 `ESP_FAIL`。

### 根因 2：USART2 TX 引脚硬件故障导致 ESP8266 从未收到指令

`HAL_UART_Transmit(&huart2, "AT\r\n", ...)` 阻塞发送未报错（函数正常返回 HAL_OK），但**PA2(USART2_TX) 引脚实际输出全为 0x00**（Break 条件/UART 帧错误）。ESP8266 从未收到有效 AT 指令，因此无任何回复。

RX 线（PA3）处于浮空/低电平状态，噪声被 UART 解析为连续的 `0x00` 帧（FE=1 帧错误），DMA 捕获后存入缓冲区。

| 诊断信息 | 含义 |
|---------|------|
| `RX_STA = 0x8003` | 3 字节帧完成（就绪 + 长度 3） |
| `buf[0..2] = 0x00` | 接收数据为 Break/噪声 |
| `USART_SR & 0x01 = 1` (FE) | 帧错误——RX 线电平异常 |
| `HAL_UART_Transmit` 返回 `HAL_OK` | TX 阻塞发送不检测引脚电平 |

### 根因 3：ISR 内阻塞 UART_Printf 导致 HAL 状态机卡死

在 `HAL_UARTEx_RxEventCallback`（USART2 中断上下文）中调用 `UART_Printf(&huart1, ...)`，其中 `HAL_UART_Transmit` 是**阻塞函数**。在 ISR 优先级 5 的环境下，USART1 TX 轮询可能失败，导致 `huart1.gState` 卡在 `BUSY_TX` 状态，后续所有 USART1 发送静默返回 `HAL_BUSY`，上位机终端看不到任何输出。

## 三、实验设计

| # | 实验 | 预期 | 实际结果 |
|---|------|------|---------|
| 1 | `HAL_UARTEx_ReceiveToIdle_DMA` 前加 `__HAL_UART_CLEAR_IDLEFLAG` | Size 不再为 0 | 通过：Size 从 0 变为实际接收字节数 |
| 2 | 回调中打印 `USART_SR` 和 `RX_BUF[0..2]` | 判别 FE 标志和实际数据 | 通过：SR 显示 FE=1 + buf 全 0x00 |
| 3 | ESP8266 从 USART2 改接 USART1 | 通信正常 | 通过：确认 USART2 TX 硬件故障 |
| 4 | ISR 中阻塞打印改为标记位 + 任务消费 | 上位机恢复输出 | 通过：上位机可收到调试信息 |

## 四、验证实验

1. **修复 1 验证**：编译烧录后 `__HAL_UART_CLEAR_IDLEFLAG` 使 IDLE 回调 Size 正确（从 0 变为实际值）
2. **修复 2 验证**：`Serial_DebugPrint()` 在 `wait_for()` 任务轮询中消费诊断标记，USART1 终端可看到诊断信息
3. **换口验证**：`mqtt_cfg.huart = &huart1`，ESP8266 连 PB6/PB7，`ESP8266_Init` 成功，MQTT 发布正常
4. **移除 ISR 阻塞发送后**：`huart1.gState` 恢复 `READY`，所有 `UART_Printf` 正常输出

## 经验教训

1. **HAL_UARTEx_ReceiveToIdle_DMA 首次调用前必须清 IDLE**：`__HAL_UART_CLEAR_IDLEFLAG(huart);`
2. **ISR 中严禁调用阻塞 HAL 函数**：`HAL_UART_Transmit`（含 `HAL_MAX_DELAY`）必须在任务/主循环上下文使用
3. **TX 正常返回不等于引脚正常输出**：HAL 阻塞发送只检查寄存器标志（TXE/TC），不检测引脚电平
4. **DMA 接收全 0x00 第一步查 SR 寄存器**：`USART_SR.FE=1` 说明物理层异常，优先排查引脚/接线
5. **USART DMA 诊断三板斧**：1.清 IDLE 2.读 SR(FE/ORE/NE) 3.ISR 只置标记不阻塞

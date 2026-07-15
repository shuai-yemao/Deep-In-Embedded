日期：2026.07

文章标签： #BSP #IIC #AHT21 #STM32F411CEU6

## 知识点 1：根据数据手册编写 AHT21 的模拟 IIC

### 实际意义

AHT21 使用两线 I2C 接口。`bsp_aht21_iic.c` 不调用 STM32 硬件 I2C 外设，而是通过 GPIO 电平变化和微秒延时直接生成总线时序，因此可以把协议层复用到不同 MCU，只需替换 GPIO 适配函数。

### AHT21 数据手册要点

| 项目 | 内容 |
| --- | --- |
| 7 位从机地址 | `0x38` |
| 写地址字节 | `0x70`（`0x38 << 1`） |
| 读地址字节 | `0x71`（`(0x38 << 1) + 1`） |
| 触发测量 | `0xAC 0x33 0x00` |
| 读取状态 | `0x71` 命令后重新起始，再读取 1 字节 |
| 软复位 | `0xBA` |
| 测量返回 | 状态字节 + 20 位湿度 + 20 位温度 + CRC 字节 |

> [!warning] 地址的常见混淆
> 数据手册给出的 `0x38` 是 7 位地址；`IICSendByte()` 发送前会左移地址，所以调用底层接口时传入 `0x38`，不要提前传入 `0x70`。

### 核心逻辑/原理

#### 1. 平台适配层

头文件中的 `iic_gpio_ops_t` 将硬件操作抽象为四个函数：`pf_gpio_init`、`pf_gpio_write`、`pf_gpio_read` 和 `pf_delay_us`。`iic_bus_t` 保存 SDA/SCL 端口、引脚、输入输出模式以及操作表。这样 `bsp_aht21_iic.c` 只关心协议，不依赖 `HAL_GPIO_*`。

SDA 必须支持“输出低电平”和“释放为输入/高电平”两种状态；SCL 也应使用开漏或等效的释放方式，并配置外部上拉电阻。总线空闲状态是 `SCL=1、SDA=1`。

#### 2. I2C 基本时序

1. **Start**：SCL、SDA 均为高时，将 SDA 拉低，再将 SCL 拉低。
2. **发送 1 字节**：按 MSB first 发送 8 位。每一位都在 SCL 低电平期间设置 SDA，在 SCL 高电平期间保持稳定供从机采样。
3. **ACK/NACK**：第 9 个时钟周期由接收方控制 SDA。接收方拉低 SDA 是 ACK，保持释放/高电平是 NACK。
4. **接收 1 字节**：主机释放 SDA，在 SCL 高电平期间读取 SDA，并按位左移拼接。
5. **Stop**：先保持 SDA 低并拉高 SCL，最后在 SCL 高电平期间释放 SDA。

`IICStart()`、`IICStop()`、`IICSendByte()`、`IICReceiveByte()`、`IICWaitAck()`、`IICSendAck()` 和 `IICSendNotAck()` 分别对应这些时序。

#### 3. AHT21 测量事务

```text
Start → 0x70 → ACK → 0xAC → ACK → 0x33 → ACK → 0x00 → ACK → Stop
等待测量完成（状态位 bit7 为 0）
Start → 0x71 → ACK → 读取 7 字节 → 前 6 字节后 ACK → 最后一字节后 NACK → Stop
```

状态字节的 bit7 是忙标志，bit3 表示校准使能。6 个数据字节按下式拼接：

```text
raw_humidity = (data[1] << 12) | (data[2] << 4) | (data[3] >> 4)
raw_temperature = ((data[3] & 0x0F) << 16) | (data[4] << 8) | data[5]
humidity = raw_humidity * 100 / 2^20
temperature = raw_temperature * 200 / 2^20 - 50
```

CRC 校验应对 `data[0]` 到 `data[5]` 计算 CRC-8，再与 `data[6]` 比较；校验失败时不能把结果当作有效温湿度。

### 关键公式/结论

1. `write_address = 0x38 << 1 = 0x70`，`read_address = 0x71`。
2. 数据在 SCL 高电平期间必须稳定；SDA 只能在 SCL 低电平期间改变，Start/Stop 是例外。
3. `t_rise ≈ 0.8473 × R_p × C_b`。上拉电阻过大，上升沿慢；过小则低电平灌电流增大。
4. `IICWaitAck()` 必须有超时退出，避免 SDA 被拉死时程序永久阻塞。
5. AHT21 的测量命令属于设备层；当前 `bsp_aht21_iic.c` 是通用 IIC 传输层，不应把 AHT21 命令硬编码进通用 `IIC_Write_*`/`IIC_Read_*` 接口。

### 代码与接口对应

| 代码接口 | 作用 |
| --- | --- |
| `IICInit()` | 配置 SDA/SCL，进入总线空闲状态 |
| `IICStart()` / `IICStop()` | 生成起始/停止条件 |
| `IICSendByte()` | MSB first 发送 8 位 |
| `IICReceiveByte()` | 读取 8 位并拼接为字节 |
| `IICWaitAck()` | 释放 SDA，读取从机 ACK，并带超时 |
| `IICSendAck()` / `IICSendNotAck()` | 主机读取多字节时应答或结束读取 |
| `IIC_Write_Multi_Byte()` | 通用写事务；AHT21 可用于发送测量命令 |
| `IIC_Read_Multi_Byte()` | 通用连续读取；需确认具体 AHT21 状态读取是否要求 Stop 后重新 Start |

### 实际操作步骤

1. 根据原理图确认 SDA/SCL 引脚、供电电压和外部上拉电阻。
2. 实现 `iic_gpio_ops_t`，确保 GPIO 释放时不会主动推高总线。
3. 调用 `IICInit()`，先用逻辑分析仪确认空闲、Start、Stop 和 9 位 ACK 波形。
4. 发送 `0xAC 0x33 0x00`，轮询状态 bit7，等待测量完成。
5. 读取 7 字节，验证 CRC，再拼接原始湿度/温度值并换算。
6. 分别测试无器件、SDA 被拉低、ACK 超时、CRC 错误和正常测量，确认错误路径会返回而不是死循环。

### 常见问题

| 现象 | 可能原因 | 检查方向 |
| --- | --- | --- |
| 一直收不到 ACK | 地址位宽错误、无上拉、引脚接反 | 确认发送 `0x70/0x71`，检查总线电平 |
| 读到的数据全为 0 或 1 | SDA 未切换为输入/释放 | 检查 `SDA_Input_Mode()` 和 GPIO 模式 |
| 读状态偶发失败 | 把 AHT21 状态读取误写成 repeated start | 按数据手册确认 Stop/Start 时序 |
| 程序卡死 | 等 ACK 没有超时 | 保留 `IIC_ACK_TIMEOUT` 保护 |
| 温湿度异常 | 位拼接、换算或 CRC 错误 | 先打印 7 原始字节，再逐步验证公式 |

---

## 📎 相关资料

### 📄 代码/附件

- [[AHT21数据手册]]
- [[I2C]]
- [[AHT21的driver文件架构设计思路]]

---

## 💬 Q&A

> 自问自答，检验理解深度。按难度递进排列。

### 🟢 基础

#### Q 1: 为什么要用模拟 IIC?与硬件 IIC 有何区别？

A 1：

1. 使用 GPIO 来模拟 IIC 通信协议，任意 IO 口都可以模拟来使用 IIC 通信协议，灵活性高；但是对于 IIC 延时精度要求高的外设和需要高速 IIC 的丢包率高

#### Q 2: SDA 信号建立时间是什么？上拉电阻是如何影响 SDA 和 SCL 的上拉时间？

A 2：

1. **SDA 信号建立时间**（t_SU;DAT）：SDA 数据线在 SCL 上升沿到来之前必须保持稳定的最短时间。I2C 规范要求：Standard Mode（100kHz）≥ 250ns，Fast Mode（400kHz）≥ 100ns。如果 SDA 还没稳定 SCL 上升沿就到了，从机采样到不确定电平，导致数据位错误。
2. **上拉电阻影响上升时间的公式**：I2C 总线的上升时间由 RC 充电决定，核心公式为：

$$
t_{rise} = 0.8473 \times R_p \times C_b
$$

其中：$R_p$ = 上拉电阻阻值（Ω），$C_b$ = 总线寄生电容（F），$t_{rise}$ = 上升时间（s），即电压从 V_IL_max 上升到 V_IH_min 所需时间。

物理模型：GPIO 开漏输出释放时 NMOS 截止，VCC 通过 Rp 向总线电容 Cb 充电，Rp 越大充电越慢，上升沿越缓。若 Rp × Cb 过大，上升时间超过规范上限（Standard Mode 1000ns / Fast Mode 300ns），从机在 SCL 高电平期间采不到有效电平。典型 3.3V 系统推荐 Rp = 4.7kΩ（标准模式）或 2.2kΩ（快速模式）。

### 🟡 进阶

#### Q 3: 为什么叫释放 SCL 和释放 SDA 信号?

A 3：

1. SCL 和 SDA 的 GPIO 输出模式设置为开漏输出模式，让上拉电阻来将 SCL 和 SDA 电压输出拉高，防止总线资源冲突和从机电压不同导致的短路
2. 释放信号，即让 GPIO 硬件结构中的 NMOS 截止，让电源对电容持续充电来拉高输出电压

### 🔴 困难

#### Q 4

A 4：

---

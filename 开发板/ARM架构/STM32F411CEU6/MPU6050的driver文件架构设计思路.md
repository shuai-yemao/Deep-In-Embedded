# 📖 引言

> 这篇笔记要讲什么？用一句话概括核心主题。

Bsp 层从 core 层芯片平台接收到通信接口和 tick 信号的同时，IRQ 和 DMA 的处理进阶的标准

---

# 📝driver 文件的设计思路

> 用一句话说清楚这个知识点是什么。

如何处理 bsp_xxx_driver 中内部实现的中断回调函数和 dma 中断回调函数与 adapter 层的芯片平台实际外部中断回调函数和 dma 回调函数的转接，以及 driver 中的中断回调函数如何处理异步处理和通知

## 实际意义

> 为什么会有该知识点？解决了什么实际问题？

MPU6050 是一个 I2C 接口的六轴 IMU 芯片（加速度计 + 陀螺仪各三轴）。驱动层的核心任务只有一件：**把寄存器操作封装成可靠的函数调用，让上层不用关心 I2C 时序、寄存器地址、位域拼接**。

AHT21 驱动（[[AHT21的driver文件架构设计思路]]）用同步阻塞模式（发命令→等→读），不需要中断。但 MPU6050 不同——数据由 INT 引脚以最高 8kHz 触发，数据量大适合 DMA，必须走**异步中断驱动**路线。

本项目文件拆成了清晰的四层，各司其职：

```mermaid
graph TB
    subgraph App["应用层"]
        APP["姿态解算 / 业务逻辑"]
    end

    subgraph Handler["Handler 层<br/>bsp_imu_handler"]
        H_INST["实例管理<br/>注册/注销/多IMU"]
        H_THREAD["读取线程<br/>队列消费/解码/限频"]
        H_ISR["ISR 桥接<br/>帧复制 + FromISR投递"]
    end

    subgraph Driver["Driver 层<br/>bsp_mpu6050_driver"]
        D_REG["寄存器读写<br/>write_reg / read_regs"]
        D_INIT["生命周期<br/>init / deinit / sleep / wakeup"]
        D_SYNC["同步采集<br/>get_data（burst read 14字节）"]
        D_DMA["DMA采集<br/>irq/dma_callback + 双缓冲"]
    end

    subgraph Adapter["Adapter 层（平台实现）"]
        A_I2C["I2C 时序<br/>软件模拟 / 硬件I2C"]
        A_TICK["时基<br/>滴答计数 / 延时"]
        A_IRQ["中断管理<br/>EXTI 屏蔽/恢复"]
        A_DMA["DMA 传输<br/>通道配置 / 双缓冲"]
    end

    APP -->|"imu_handler_event_t"| Handler
    Handler -->|"mpu6050_ops_t 接口表"| Driver
    Driver -->|"iic/timebase/irq/dma 接口"| Adapter
    Adapter -.->|"HAL 库 / 寄存器"| HW["STM32F411 硬件"]
```

- **Config 层** (`bsp_mpu6050_config.h`)：纯寄存器地址、位域掩码、配置结构体，零运行时逻辑。相当于芯片手册的 C 语言翻译。
- **Driver 层** (`bsp_mpu6050_driver.h/.c`)：寄存器级读写、设备生命周期、同步/DMA 采集。**不调任何 HAL 或 OS 函数**。
- **Handler 层** (`bsp_imu_handler.h/.c`)：多 IMU 实例管理、RTOS 线程调度、DMA ISR 到任务上下文的桥接。
- **Adapter 层**（由平台开发者实现）：I2C 时序、时基、中断、DMA 的具体 HAL 实现。

### 1. 依赖注入：Driver 不依赖任何具体硬件

Driver 所有外部依赖通过操作表指针注入，视角里只有接口：

```mermaid
graph LR
    subgraph "Driver 内部（纯逻辑）"
        CORE["寄存器读写<br/>初始化序列<br/>数据拼接<br/>状态机"]
    end

    subgraph "外部依赖（接口注入）"
        I1["iic_driver_interface_t<br/>send_bytes / receive_bytes"]
        I2["timebase_interface_t<br/>delay_ms / get_tick_count"]
        I3["irq_interface_t<br/>mask / unmask data_ready"]
        I4["dma_interface_t<br/>dma_init / dma_buffer"]
    end

    CORE -.->|"通过 p_ops_instance 访问"| I1
    CORE -.->|"通过 p_ops_instance 访问"| I2
    CORE -.->|"通过 p_ops_instance 访问"| I3
    CORE -.->|"通过 p_ops_instance 访问"| I4
```

**换 MCU 只改 Adapter 层**，Driver 和 Handler 原封不动。单元测试时注入 mock 函数，PC 上就能跑全部驱动逻辑，不用每次烧片验证。

### 2. 中断上下半部分离

ISR 执行期间同级和低优先级中断全部被阻塞。如果在 EXTI ISR 里直接跑姿态解算，SysTick 会被阻塞导致 FreeRTOS 时基偏移。

```
上半部 (ISR, <5μs):  关EXTI → 查INT_STATUS → 启动DMA / 复制帧到队列
下半部 (任务上下文):  解码 → lifetime限频 → 更新latest_data → 发布回调
```

ISR 里只做内存拷贝和 FromISR 队列投递，不解码、不算时间、不打日志。重活全扔到读取线程。

### 3. 消息队列统一调度

不管消息来自任务还是 ISR，最终都在同一条读取线程里串行处理。没有并发竞争，不需要互斥锁。线程永久阻塞在 `pf_os_queue_get(WAIT_FOREVER)` 上，没消息时零 CPU 占用。

### 4. 多实例支持

Handler 通过 `imu_handler_sensor_ops_t` 操作表管理最多 3 个 IMU 实例。每个实例独立维护操作表，DMA 通知绑定在注册时自动完成，`instance_index` 贯穿整条数据链路。

## 应用场景

> 在实际中主要被用来做什么？

### 1. DMA + 中断驱动的高频采集（核心场景）

```mermaid
sequenceDiagram
    participant MPU as MPU6050
    participant EXTI as MCU EXTI
    participant DRV as Driver<br/>irq/dma_callback
    participant BUF as DMA 双缓冲
    participant ISR_BRG as Handler<br/>ISR 桥接
    participant Q as 消息队列
    participant THD as 读取线程
    participant APP as 应用层

    MPU->>EXTI: INT 引脚上升沿（数据就绪）
    EXTI->>DRV: pf_irq_callback()
    Note over DRV: ① 关 EXTI<br/>② 读 INT_STATUS 确认数据就绪<br/>③ 启动 I2C RX DMA
    DRV->>MPU: DMA 读取 14 字节传感器数据
    MPU->>BUF: 数据写入 write_buffer
    BUF->>DRV: DMA 完成中断 → pf_dma_callback()
    Note over DRV: ④ 交换 read/write 指针<br/>⑤ 调 pf_dma_notify 发帧副本
    DRV->>ISR_BRG: pf_dma_notify(frame, size, context)
    Note over ISR_BRG: ISR 内仅做：<br/>• 复制帧到消息体<br/>• FromISR 队列发送<br/>不解码、不算时间、不打日志
    ISR_BRG->>Q: pf_os_queue_put_from_isr()
    Q->>THD: pf_os_queue_get(WAIT_FOREVER)
    Note over THD: ⑥ pf_decode_frame 解码<br/>⑦ lifetime 限频（默认50ms）<br/>⑧ 更新 latest_data<br/>⑨ 触发 pf_data_callback
    THD->>APP: pf_data_callback(&data, context)
```

整条链路 ISR 里只做内存拷贝和队列投递，解码和限频推迟到线程。适用于姿态解算、振动分析等需要稳定采样周期的场景。

### 2. 单传感器轮询读取

裸机或 RTOS 下，定时调用 `pf_get_data` 同步读 14 字节传感器数据。Driver 一次 burst read 保证三轴数据属于同一采样时刻。适用于对实时性要求不高、不想引入 DMA 复杂度的场景。

### 3. 多 IMU 实例管理

Handler 支持注册最多 3 个传感器实例，每个独立维护操作表和 DMA 通知绑定。适用于需要多 IMU 冗余或不同位置姿态采集的场景（如机器人关节角度测量）。

### 4. 跨平台复用

Driver 层不调任何 HAL 或 OS 函数，换 MCU 只改写 Adapter 层。同一套代码在 STM32F4 / GD32 / ESP32 间复用，变动的只有 I2C 时序和 DMA 通道配置。

### 5. 单元测试 / Mock 调试

因为全部依赖通过接口注入，PC 上注入 mock 的 I2C 和时基函数就能跑全部驱动逻辑，不需要硬件。GPIO trace 接口可接逻辑分析仪观测中断回调进出时间。

## 核心逻辑/原理

> 它是如何工作的？拆解背后的机制。

### 1. 接口注入解耦

Driver 运行时，不调任何 HAL 或 OS 函数。`bsp_mpu6050_driver_t` 通过 `p_ops_instance` 指针访问所有外部依赖：

```mermaid
classDiagram
    class bsp_mpu6050_driver_t {
        +is_inited
        +p_ops_instance
        +pf_init()
        +pf_deinit()
        +pf_get_data()
        +pf_sleep() / pf_wakeup()
        +pf_set_config()
        +pf_irq_callback()
        +pf_dma_callback()
        +pf_set_dma_notify()
    }

    class mpu6050_ops_t {
        +p_iic_driver_instance
        +p_timebase_instance
        +p_irq_instance
        +p_dma_instance
        +p_trace_instance
    }

    class iic_driver_interface_t {
        +pf_send_bytes()
        +pf_receive_bytes()
        +pf_start() / pf_stop()
        +pf_wait_ack()
    }

    class bsp_imu_handler_t {
        +is_inited
        +imu_instance (max 3)
        +queue_handler
        +thread_handler
        +latest_data
        +pf_init()
        +pf_deinit()
        +pf_instance_register()
    }

    class imu_handler_os_t {
        +p_os_queue
        +p_os_thread
        +p_os_semaphore
    }

    bsp_mpu6050_driver_t --> mpu6050_ops_t : 持有指针
    mpu6050_ops_t --> iic_driver_interface_t : 持有指针
    bsp_imu_handler_t --> imu_handler_os_t : 持有指针
    bsp_imu_handler_t ..> bsp_mpu6050_driver_t : 通过 imu_handler_sensor_ops_t 间接调用
```

### 2. DMA 双缓冲 + 中断安全

```mermaid
graph TB
    subgraph "DMA 硬件"
        DMA_CTRL["I2C RX DMA<br/>目标：write_buffer"]
    end

    subgraph "双缓冲区"
        WB["write_buffer<br/>DMA 正在写入"]
        RB["read_buffer<br/>任务线程正在读取"]
    end

    subgraph "DMA 完成 ISR"
        SWAP["指针交换<br/>write ↔ read"]
        NOTIFY["通知 Handler<br/>pf_dma_notify(frame, size, ctx)"]
    end

    subgraph "Handler ISR 桥接"
        COPY["复制帧到消息体"]
        QPUT["FromISR 队列发送"]
    end

    subgraph "读取线程"
        DECODE["pf_decode_frame 解码"]
        LIMIT["lifetime 限频 (50ms)"]
        CB["pf_data_callback 发布"]
    end

    DMA_CTRL -->|"写满后触发"| SWAP
    SWAP -->|"旧 write → 新 read"| RB
    SWAP -->|"旧 read → 新 write"| WB
    SWAP --> NOTIFY
    NOTIFY --> COPY
    COPY --> QPUT
    QPUT -->|"消息队列"| DECODE
    DECODE --> LIMIT
    LIMIT --> CB
```

关键设计点：

- 指针交换发生在 EXTI 关闭期间，不会和新数据中断竞争。
- 永远有一个 buffer 是稳定的——任务线程读 `read_buffer` 时 DMA 在写 `write_buffer`，互不干扰。
- ISR 里只复制帧到消息体，不解码不计算——重活推迟到线程。

### 3. lifetime 限频

```mermaid
graph TD
    A["收到 DMA 帧或同步读取请求"] --> B{"当前时间 - 上次处理时间<br/>≥ lifetime？"}
    B -->|"是"| C["更新 last_time<br/>执行解码/读取"]
    B -->|"否"| D["跳过，直接返回 OK"]
    C --> E["触发回调 / 更新 latest_data"]
```

四种读取类型（ACCEL / GYRO / TEMPERATURE / ALL）各自独立计时。DMA 路径默认 50ms 限频——MPU6050 数据就绪可达 8kHz，但读取线程没必跟着跑那么快，限频保证 CPU 不被打满。

### 4. 消息队列统一调度

```mermaid
graph TB
    subgraph "生产者"
        TASK["应用任务<br/>post_read_event()"]
        ISR["DMA ISR<br/>dma_notify_from_isr()"]
    end

    subgraph "消息队列"
        Q["imu_handler_message_t 队列<br/>深度: 8"]
    end

    subgraph "消费者（单一读取线程）"
        GET["阻塞等待<br/>pf_os_queue_get(WAIT_FOREVER)"]
        SWITCH{"message.type ?"}
        EVT["READ_EVENT<br/>→ bsp_read_imu()<br/>寄存器同步读取"]
        DMA_MSG["DMA_FRAME<br/>→ imu_handler_process_dma_frame()<br/>解码 + 限频 + 回调"]
    end

    TASK --> Q
    ISR --> Q
    Q --> GET
    GET --> SWITCH
    SWITCH --> EVT
    SWITCH --> DMA_MSG
```

不管消息来源是任务还是 ISR，最终都在同一条线程串行处理。没有并发竞争，不需要互斥锁。线程没消息时永久阻塞，零 CPU 占用。

### 5. 函数指针表模式

Driver 所有方法（`pf_init`、`pf_get_data`、`pf_sleep` 等）在 `bsp_mpu6050_driver_inst()` 里一次性绑定到静态函数。上层调 `driver->pf_get_data(&driver, ...)`，不关心后面是 MPU6050 还是未来换的 ICM-20948。Handler 同理——依赖的是 `imu_handler_sensor_ops_t` 操作表，不直接碰 `bsp_mpu6050_driver_t`。

## 关键公式/结论

> 最终结论和公式。数据在 Driver 层只返回原始码值，单位换算在上层完成。

### 原始数据 Burst Read 布局

一次 I2C burst read 从 `ACCEL_XOUT_H`（0x3B）开始连读 14 字节，寄存器高字节在前：

| 字节偏移 | 寄存器 | 对应字段 |
|---------|--------|---------|
| 0 ~ 1 | ACCEL_XOUT | `accel_x` |
| 2 ~ 3 | ACCEL_YOUT | `accel_y` |
| 4 ~ 5 | ACCEL_ZOUT | `accel_z` |
| 6 ~ 7 | TEMP_OUT | `temperature` |
| 8 ~ 9 | GYRO_XOUT | `gyro_x` |
| 10 ~ 11 | GYRO_YOUT | `gyro_y` |
| 12 ~ 13 | GYRO_ZOUT | `gyro_z` |

拼接方式（源码 `mpu6050_get_data` 中实现）：

```c
accel_x = (int16_t)(((uint16_t)raw[0] << 8) | raw[1]);   // 大端组合
```

### 加速度计单位换算

| FS_SEL | 量程 | 灵敏度 (LSB/g) | 换算公式 |
|--------|------|---------------|---------|
| 0 | ±2g | 16384 | `g = raw / 16384.0f` |
| 1 | ±4g | 8192 | `g = raw / 8192.0f` |
| 2 | ±8g | 4096 | `g = raw / 4096.0f` |
| 3 | ±16g | 2048 | `g = raw / 2048.0f` |

源码对应 `mpu6050_accel_fs_t` 枚举，位域位于 `ACCEL_CONFIG[4:3]`。

### 陀螺仪单位换算

| FS_SEL | 量程 | 灵敏度 (LSB/dps) | 换算公式 |
|--------|------|-----------------|---------|
| 0 | ±250°/s | 131 | `dps = raw / 131.0f` |
| 1 | ±500°/s | 65.5 | `dps = raw / 65.5f` |
| 2 | ±1000°/s | 32.8 | `dps = raw / 32.8f` |
| 3 | ±2000°/s | 16.4 | `dps = raw / 16.4f` |

源码对应 `mpu6050_gyro_fs_t` 枚举，位域位于 `GYRO_CONFIG[4:3]`。

### 温度换算

```
T(°C) = TEMP_OUT / 340.0f + 36.53f
```

### 采样率计算

```
Sample Rate = Gyroscope Output Rate / (1 + SMPLRT_DIV)

其中 Gyroscope Output Rate:
- DLPF 使能时（DLPF_CFG ∈ [0,6]）: 8kHz
- DLPF 禁用时（DLPF_CFG ∈ [7] 或 FCHOICE_B = 00）: 1kHz
```

源码默认 `SMPLRT_DIV = 7`、`DLPF_CFG = 3`，实际采样率 = 8kHz / (1+7) = **1kHz**。

### DLPF 带宽参考

| DLPF_CFG | 加速度计带宽 | 陀螺仪带宽 | 延迟 |
|----------|------------|-----------|------|
| 0 | 260 Hz | 256 Hz | 0.98 ms |
| 1 | 184 Hz | 188 Hz | 1.9 ms |
| 2 | 94 Hz | 98 Hz | 2.8 ms |
| 3 | 44 Hz | 42 Hz | 4.8 ms |
| 4 | 21 Hz | 20 Hz | 9.7 ms |
| 5 | 10 Hz | 10 Hz | 18.8 ms |
| 6 | 5 Hz | 5 Hz | 33.5 ms |

源码默认取 3（44/42 Hz），平衡噪声抑制和响应速度。值 7 为保留值，不可使用（`mpu6050_set_config` 中做范围校验）。

### DMA 双缓冲指针交换

```
完成前:  write_buf → Buf_A,  read_buf → Buf_B
完成后:  write_buf → Buf_B,  read_buf → Buf_A  (交换)
通知:    pf_dma_notify(read_buf) — 上层读到刚完成的帧
```

### 默认配置快照

源码 `mpu6050_init` 中写入的默认值（对应 `mpu6050_config_t`）：

| 参数 | 默认值 | 后果 |
|------|--------|------|
| `sample_rate_div = 7` | 1kHz 采样 | |
| `dlpf_cfg = 3` | 44/42 Hz 带宽 | |
| `accel_fs = 0` | ±2g | |
| `gyro_fs = 0` | ±250°/s | |
| `int_enable = DATA_RDY_EN` | 仅数据就绪中断 | |

## 实际操作步骤

> 动手验证/配置的具体操作。

### 启动阶段

**第一步 — Adapter 实现接口**

平台开发者实现四个接口结构体并填好函数指针：

```c
// 1. I2C 接口 — 对接 HAL_I2C_Mem_Write / HAL_I2C_Mem_Read
iic_driver_interface_t i2c_if = {
    .pf_send_bytes    = my_i2c_send_bytes,    // 封装 HAL_I2C_Mem_Write
    .pf_receive_bytes = my_i2c_receive_bytes, // 封装 HAL_I2C_Mem_Read
    .pf_start         = my_i2c_start,
    .pf_stop          = my_i2c_stop,
    // ...
};

// 2. 时基接口
timebase_interface_t tick_if = {
    .pf_delay_ms = HAL_Delay,          // 或 FreeRTOS vTaskDelay
    .pf_delay_us = my_delay_us,
};

// 3. 中断接口（关键 — 负责 EXTI 屏蔽/恢复）
irq_interface_t irq_if = {
    .pf_mask_data_ready_irq   = HAL_NVIC_DisableIRQ, // 关 MPU6050 EXTI
    .pf_unmask_data_ready_irq = HAL_NVIC_EnableIRQ,  // 开 MPU6050 EXTI
};

// 4. DMA 接口（可选）
dma_interface_t dma_if = { .pf_dma_init = my_dma_start, /* ... */ };
```

**第二步 — 聚合接口、实例化 Driver**

```c
mpu6050_ops_t ops = {
    .p_iic_driver_instance = &i2c_if,
    .p_timebase_instance   = &tick_if,
    .p_irq_instance        = &irq_if,
    .p_dma_instance        = &dma_if,  // NULL 则禁用 DMA
};

bsp_mpu6050_driver_t mpu_drv = {0};
mpu6050_status_t ret = bsp_mpu6050_driver_inst(&mpu_drv, &ops);
// 内部执行：绑定方法表 → 复位设备 → 等100ms → 选PLL时钟
//          → 写采样率/量程/DLPF/中断配置 → 读 WHO_AM_I 校验
```

**第三步 — 实例化 Handler、注册 Driver**

```c
// Adapter 实现传感器操作表
imu_handler_sensor_ops_t sensor_ops = {
    .pf_init       = my_imu_init,        // 调 driver->pf_init()
    .pf_deinit     = my_imu_deinit,      // 调 driver->pf_deinit()
    .pf_read_data  = my_imu_read,        // 调 driver->pf_get_data()
    .pf_decode_frame = my_decode_frame,  // 把14字节原始帧 → imu_handler_data_t
    .pf_set_dma_notify = my_set_dma_notify, // 调 driver->pf_set_dma_notify()
    .pf_detect     = my_imu_detect,      // 调 driver->pf_read_id()
};

// 初始化 Handler（内部创建信号量→队列→读取线程）
bsp_imu_handler_t handler = {0};
imu_handler_os_t os_ops = { .p_os_queue = &queue_if, /* ... */ };
bsp_imu_handler_inst(&handler, &tick_if, &os_ops);

// 注册 Driver 实例（自动绑定 DMA 通知）
handler.pf_instance_register(&handler, &mpu_drv, &sensor_ops);
```

### 运行阶段

**路径 A — 任务同步读取**

```c
imu_handler_data_t data = {0};
imu_handler_event_t event = {
    .data        = &data,
    .lifetime    = 20,                    // 最小间隔 20ms
    .read_status = IMU_HANDLER_READ_ALL,
    .pf_event_callback = my_data_ready,   // 读取完成后回调
};
handler_post_read_event(&handler, &event, 100); // 超时 100ms
// → 队列 → 读取线程 → bsp_read_imu → pf_read_data → pf_get_data
//   → I2C burst read 14字节 → 拼 int16_t → my_data_ready(&data)
```

**路径 B — DMA 异步读取（零代码介入）**

应用层只需在启动期注册数据回调，之后 DMA 全自动运行：

```c
void on_imu_data(const imu_handler_data_t *data, void *ctx) {
    // data 已经是解码后的 int16_t 原始值，单位换算在上层做
    float ax = data->accel_x / 16384.0f;  // ±2g 量程
    float gz = data->gyro_z  / 131.0f;    // ±250dps 量程
    // 喂给姿态解算...
}

bsp_imu_handler_set_data_callback(&handler, on_imu_data, NULL);

// 之后一切自动：
// MPU6050 INT → EXTI ISR → Driver.irq_callback → DMA → Driver.dma_callback
//   → Handler ISR 桥接 → 队列 → 读取线程 → on_imu_data()
```

**路径 C — 调试观测**

Driver 的 trace 接口可接逻辑分析仪：

```c
// Adapter 实现 trace 接口（直接用 GPIO 翻转）
void trace_pin_set(uint8_t level) {
    HAL_GPIO_WritePin(TRACE_GPIO_Port, TRACE_Pin, level ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
// 逻辑分析仪抓 TRACE_Pin：
//   level=1 → 进入 irq_callback/dma_callback
//   level=0 → 退出
// 可精确测量 ISR 执行时长和 DMA 完成间隔
```

### 停止阶段

```c
// 按创建逆序释放
handler.pf_deinit(&handler);  // 删线程 → 删队列 → 删信号量 → 清实例引用
mpu_drv.pf_deinit(&mpu_drv);  // 写 PWR_MGMT_1.SLEEP → 标记 NOT_INITED
```

## 常见问题

> 现象 → 根因 → 修复。均来自本项目的实际调试经历。

### 问题 1：`bsp_mpu6050_driver_inst` 返回 `MPU6050_ERRORTIMEOUT`

**现象**：初始化函数调用返回错误，日志打印 `stage=hw_init, result=failed, step=config_or_id_check`。

**根因**：硬件初始化成功后，`mpu6050_set_config` 写寄存器或 `mpu6050_read_id` 读 WHO_AM_I 失败。最常见原因是 I2C 总线不通——SDA/SCL 引脚映射错误、上拉电阻缺失（I2C 标准要求 4.7kΩ 上拉到 VCC）、或 MPU6050 未供电。

**定位**：先在 `mpu6050_read_id` 入口加断点，看 `mpu6050_read_regs(self, 0x75, &id, 1)` 的返回值。如果返回 `MPU6050_ERRORTIMEOUT`，用逻辑分析仪抓 I2C 波形——SDA 被从机一直拉高不放 = 地址不对或芯片没响应。

**修复**：

1. 检查 `AD0` 引脚电平 → 决定地址是 0x68 还是 0x69
2. 用万用表量 SDA/SCL 对 VCC 的电压 → 确保上拉电阻存在
3. 在 `bsp_mpu6050_driver_inst` 之前确认 `HAL_I2C_IsDeviceReady(&hi2c1, 0x68<<1, 3, 100)` 返回 HAL_OK

### 问题 2：DMA 回调一直不来，中断被吞

**现象**：MPU6050 INT 引脚有波形（示波器可见 200Hz 方波），但 Handler 的 `pf_data_callback` 从未触发。

**根因**：两类可能——

- **EXTI 未正确使能**：MCU 外部中断线未配置或被更高优先级中断抢占。检查 NVIC 优先级配置——MPU6050 EXTI 的抢占优先级不能低于正在阻塞它的中断。
- **DMA 启动失败后 EXTI 未恢复**：源码 `mpu6050_irq_callback` 中，如果 `INT_STATUS` 检查不通过或 `dma_init` 失败，会走 `callback_restore_irq` 分支恢复 EXTI。但如果 `pf_mask_data_ready_irq` 本身成功而后续 `pf_unmask_data_ready_irq` 失败，EXTI 永远关闭。

**定位**：利用源码中预留的 trace 接口：

```c
// 逻辑分析仪抓 TRACE_Pin：
//   level=1 → 进入 irq_callback
//   level=0 → 退出
```

如果 TRACE_Pin 拉高后一直不拉低 = `irq_callback` 内某处死等或未走完正常退出路径。同时用 `DEBUG_MPU6050_OUT` 打开日志看 `stage=irq, result=ignored` 的打印频率。

**修复**：

1. 确认 `NVIC_SetPriority(EXTIx_IRQn, 5)` 优先级合理（数字越小越高，0 为最高）
2. 检查 `mpu6050_irq_callback` 的 `callback_restore_irq` 标签处是否确实调了 `pf_unmask_data_ready_irq`
3. 如果 INT_STATUS 经常不匹配（日志频繁打印 `int_status=0x00`），检查 INT 引脚配置——可能配置为电平触发而非边沿触发导致重复进中断

### 问题 3：读取线程解码失败，`latest_status` 持续为错误值

**现象**：DMA 帧正常投递到队列，但 `latest_data` 始终为零，`latest_status` 为非 OK 值。

**根因**：解码路径 `imu_handler_process_dma_frame` 的失败点：

- `pf_decode_frame` 返回错误 → Adapter 的解码实现有问题（字节序错误、帧格式不对）
- `pf_get_time_ms` 返回错误 → 时基接口未正确注入
- `lifetime` 机制让数据在 `IMU_HANDLER_DMA_LIFETIME_MS`（默认 50ms）内被跳过 → `IMU_HANDLER_OK` 返回但未解码

**修复**：

1. 检查 Adapter 的 `pf_decode_frame` 实现——确认 14 字节布局和字节序与大端一致
2. 在 `imu_handler_process_dma_frame` 的 `pf_decode_frame` 调用前后加断点，对比原始 `signal->frame` 和解码后的 `data`
3. 如果只是 lifetime 限频太激进，调小 `IMU_HANDLER_DMA_LIFETIME_MS`（项目默认为 50ms = 20Hz 等效刷新率）

### 问题 4：重复初始化或重复注册导致资源泄漏

**现象**：正常运行一段时间后 `pf_instance_register` 返回 `IMU_HANDLER_ERRORRESOURCE`。

**根因**：源码在所有入口都有状态检查——`bsp_mpu6050_driver_inst` 拒绝 `is_inited == INITED` 的实例，`imu_handler_instance_register` 拒绝重复的 instance 指针和超出 `IMU_NUM_MAX` 的场景。如果上层代码在未调 `pf_deinit` 的情况下再次调 `pf_init`，会被拦截但资源不会自动恢复。

**修复**：

1. 严格遵守生命周期：`deinit` 后才能再次 `init`
2. 注册 Instance 前用 `imu_instance.instance_num` 检查是否已达上限（3 个）
3. 如果确实需要替换实例，先调 `pf_deinit` 完整释放再重新初始化

### 问题 5：唤醒后立即读取数据全为零或漂移严重

**现象**：调 `pf_wakeup` 后立刻读数据，读数不正常。

**根因**：从 SLEEP 模式唤醒后，MPU6050 内部 PLL 需要重新锁定。手册规定典型锁定时间为 60ms，源码在 `mpu6050_init` 中 `DEVICE_RESET` 后 wait 100ms。但 `mpu6050_wakeup` 只写了 `CLOCK_PLL_XGYRO` 位，没有加延时——调用者需要自行等待。

**修复**：

```c
mpu_drv.pf_wakeup(&mpu_drv);
mpu_drv.p_ops_instance->p_timebase_instance->pf_delay_ms(100); // 等 PLL 锁
// 此后才能正常读数据
```

### 问题 7：关键共享数据缺少临界区保护 ← 当前架构待修复项

**现象**：运行时偶尔出现 `pf_data_callback` 空指针访问，或回调上下文 `context` 与回调函数不匹配。

**根因**：Handler 定义了临界区接口 `imu_handler_os_critical_t`（`pf_os_critical_enter` / `pf_os_critical_exit`），也创建了二值信号量（初始计数 1），但在当前代码中**两者均未被实际调用**。

存在两处并发风险：

| 共享资源 | 写者 | 读者 | 风险 |
|---------|------|------|------|
| `pf_data_callback` | `bsp_imu_handler_set_data_callback`（任意任务） | 读取线程 `imu_handler_process_dma_frame` | 读取线程拿到新回调 + 旧 context，回调被错误参数调用 |
| `latest_data` / `latest_status` | 读取线程 | `pf_data_callback` 内可能转发到其他任务 | 依赖回调实现是否正确，无系统级保证 |

```mermaid
sequenceDiagram
    participant TASK as 应用任务
    participant THD as 读取线程

    Note over TASK,THD: 竞态窗口：回调更新 vs 回调调用

    THD->>THD: if (NULL != pf_data_callback)  ← 检查通过
    TASK->>TASK: pf_data_callback = NULL      ← 应用清空回调
    TASK->>TASK: context = NULL
    THD->>THD: pf_data_callback(latest_data, context)
    Note over THD: 此时 pf_data_callback = NULL<br/>但检查已过，空指针崩溃！
```

**修复**：

利用已经定义好但未使用的 `p_os_critical` 接口：

```c
// === bsp_imu_handler_set_data_callback — 加临界区 ===
imu_handler_status_t bsp_imu_handler_set_data_callback(
    bsp_imu_handler_t *const self, imu_handler_data_callback_t callback,
    void *const context) {
  if (NULL == self) return IMU_HANDLER_ERRORPARAMETER;
  if (IMU_INITED != self->is_inited) return IMU_HANDLER_ERRORRESOURCE;

  // ★ 加锁
  if (NULL != self->p_imu_os && NULL != self->p_imu_os->p_os_critical &&
      NULL != self->p_imu_os->p_os_critical->pf_os_critical_enter) {
    self->p_imu_os->p_os_critical->pf_os_critical_enter();
  }

  self->pf_data_callback = callback;
  self->p_data_callback_context = (NULL != callback) ? context : NULL;

  // ★ 解锁
  if (NULL != self->p_imu_os && NULL != self->p_imu_os->p_os_critical &&
      NULL != self->p_imu_os->p_os_critical->pf_os_critical_exit) {
    self->p_imu_os->p_os_critical->pf_os_critical_exit();
  }
  return IMU_HANDLER_OK;
}

// === imu_handler_process_dma_frame — 读端同样加临界区 ===
// 在读取 pf_data_callback 指针前加锁，调用完成后解锁：
//   enter_critical();
//   imu_handler_data_callback_t cb = self->pf_data_callback;
//   void *ctx = self->p_data_callback_context;
//   exit_critical();
//   if (NULL != cb) { cb(&self->latest_data, ctx); }
```

> 当前代码中**其他路径已隐式安全**：DMA 双缓冲交换在 EXTI 关闭期间完成（天然互斥）；消息队列单消费者串行处理（无并发写 `latest_data`）。只有回调配置这条路径缺少保护。

### 问题 6：DMA 双缓冲区指针相等导致 `dma_callback` 返回 `ERRORRESOURCE`

**现象**：日志打印 DMA 回调失败，错误码 `MPU6050_ERRORRESOURCE`。

**根因**：源码 `mpu6050_dma_callback` 中有检查：

```c
if (NULL == dma->dma_buffer->read_buffer ||
    NULL == dma->dma_buffer->write_buffer ||
    dma->dma_buffer->read_buffer == dma->dma_buffer->write_buffer) {
    ret = MPU6050_ERRORRESOURCE;
}
```

`read_buffer == write_buffer` 意味着 Adapter 初始化 DMA 双缓冲时分配错误——两个指针指向了同一块内存。指针交换后不会有任何效果，且上层读到的永远是旧的 write_buffer 内容。

**修复**：Adapter 在 `dma_interface_t` 初始化时分配两块独立的内存：

```c
static uint32_t dma_buf_a[4]; // 14 字节 + 对齐
static uint32_t dma_buf_b[4];
dma_buffer_t buf = {
    .size = MPU6050_SENSOR_DATA_LENGTH,
    .read_buffer  = dma_buf_a,
    .write_buffer = dma_buf_b,  // 必须 ≠ read_buffer
};
```

---

# 💬 Q&A

> 自问自答，检验理解深度。按难度递进排列。

1.什么是时间片切割?

2.为什么少用中断在 FreeRTOS 中?

3.什么是 CPU 运行流水线?

4.FreeRTOS 切换任务时如何保存线程上下文?

1. S-Bus 的压栈时间受什么影响?
2.ICode 取向量的速度优化存在哪些方法?
1.硬件 IIC 相较于软件 IIC 是如何实现异步的?
2.IIC 的传输完成中断是如何被 CPU 作为异常处理的？信号在内核中是如何传递的?
3.一定要用信号量来做中断和线程之间的同步吗？如果不是，那应该怎么做？这样做的好处是什么？

1.中断延时在系统中是被什么影响的?

2.中断延时很高，该怎么办?

3.中断延时和中断上半部、中断下半部有什么关系吗?

## 🟢 基础

> 最基本的概念和用法，入门必知。

### Q 1

A 1：

### Q 2

A 2：

## 🟡 进阶

> 容易踩的坑和常见误区。

### Q 3

A 3：

## 🔴 困难

> 结合实战的深层原理和设计权衡。

### Q 4

A 4：

---

# 📋 总结

> 3-5 句话回顾核心要点，用自己的话复述。

---

# 📎 参考资料

> 学习过程中用到的外部资源汇总。

## 🎥 视频链接

> B 站 / YouTube 教程，优先选项目实战类和原理动画类。

- [标题](url) — 一句话说明讲了什么

## 🔗 博客/文档链接

> 分析最透彻的博客、官方文档、社区帖子。

- [标题](url) — CSDN / 博客园 / 飞书 / 知乎
- [标题](url) — 芯片厂商官方文档

## 💻 仓库链接

> GitHub / Gitee 源码仓库，含 Demo 工程和工具链。

- [owner/repo](url) — 一句话描述
- [owner/repo](url)

## 📄 代码/附件

> 本地 PDF、代码包、工具链文件。

- [[附件文件.pdf]]
- [[示例代码.zip]]

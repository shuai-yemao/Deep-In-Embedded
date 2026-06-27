---
tags:
  - SystemView
  - SEGGER
  - RTT
  - RTOS调试
  - FreeRTOS
  - STM32
created: 2026-06-07
---

# SEGGER SystemView 移植与使用指南

> **适用平台**: STM32F411CEU6 (Cortex-M4) + FreeRTOS V10.3.1 (CMSIS-RTOS2)
> **SystemView 版本**: 3.54
> **开发环境**: Keil MDK V5.42 (ARMCC V5.06)
> **调试探针**: J-Link (SWD, 4000kHz)

---

## 1. 概述

SEGGER SystemView 是一个实时记录（RTOS）系统级行为分析工具，可直观显示：

- **任务调度**：任务创建、切换、阻塞、就绪状态变化
- **中断事件**：ISR 进入/退出、中断嵌套
- **通信机制**：队列发送/接收、信号量、互斥量操作
- **CPU 负载**：各任务占用 CPU 时间片比例
- **时间线**：精确到 DWT 时钟周期的事件时序

---

## 2. 文件结构

### 2.1 SystemView 中间件文件

```
Middlewares/Third_Party/Systemview/
├── Inc/
│   ├── SEGGER_SYSVIEW.h               # 主 API 头文件
│   ├── SEGGER_SYSVIEW_Conf.h          # 项目定制配置
│   ├── SEGGER_SYSVIEW_ConfDefaults.h  # 默认配置
│   ├── SEGGER_SYSVIEW_FreeRTOS.h      # FreeRTOS trace 宏映射 ★关键
│   └── SEGGER_SYSVIEW_Int.h           # 内部头文件
└── Src/
    ├── SEGGER_SYSVIEW.c               # 主实现
    ├── SEGGER_SYSVIEW_Config_FreeRTOS.c  # FreeRTOS 配置（应用名/设备/频率）
    └── SEGGER_SYSVIEW_FreeRTOS.c      # FreeRTOS 桥接实现（任务列表/回调）
```

### 2.2 Keil 工程组结构

建议在 Keil 工程中新建 Group：`SEGGER_SystemView`，添加以下文件：

| 文件 | 必须？ | 说明 |
|------|--------|------|
| `SEGGER_SYSVIEW.c` | ✅ | 核心实现 |
| `SEGGER_SYSVIEW_Config_FreeRTOS.c` | ✅ | 平台配置 |
| `SEGGER_SYSVIEW_FreeRTOS.c` | ✅ | FreeRTOS 桥接 |

> **注意**：不需要添加 `SEGGER_RTT.c` —— SystemView 内部使用 RTT，但通过 `#include` 方式集成，无需单独编译 RTT 源文件。

---

## 3. 移植步骤

### Step 1：添加文件到工程

1. 将 `Middlewares/Third_Party/Systemview/` 复制到工程目录
2. 在 Keil MDK 中创建 Group `SEGGER_SystemView`
3. 添加上述 3 个源文件到该 Group
4. 在工程 Include Paths 中添加 SystemView 头文件路径：

   ```
   Middlewares/Third_Party/Systemview/Inc
   ```

### Step 2：配置 `SEGGER_SYSVIEW_Conf.h`

```c
// 文件：Middlewares/Third_Party/Systemview/Inc/SEGGER_SYSVIEW_Conf.h

// 初始化后自动启动录制（不需手动调用 SEGGER_SYSVIEW_Start()）
#define SEGGER_SYSVIEW_START_ON_INIT              1

// RTT 通道：建议设为 1，与 elog/channel 0 分离
#define SEGGER_SYSVIEW_RTT_CHANNEL                1

// RTT 缓冲大小：elog 活动时建议至少 4096，防止事件丢失
#define SEGGER_SYSVIEW_RTT_BUFFER_SIZE            4096
```

### Step 3：配置 `SEGGER_SYSVIEW_Config_FreeRTOS.c`

```c
// 应用名称（显示在 SystemView PC 标题栏）
#define SYSVIEW_APP_NAME        "YourAppName"

// 设备名称
#define SYSVIEW_DEVICE_NAME     "Cortex-M4"

// 时间戳频率 = CPU 核心时钟频率（SystemCoreClock）
#define SYSVIEW_TIMESTAMP_FREQ  (configCPU_CLOCK_HZ)

// CPU 频率
#define SYSVIEW_CPU_FREQ        configCPU_CLOCK_HZ

// RAM 基址（STM32F411 = 0x20000000）
#define SYSVIEW_RAM_BASE        (0x20000000)
```

`_cbSendSystemDesc()` 回调中声明系统描述和中断：

```c
static void _cbSendSystemDesc(void) {
  SEGGER_SYSVIEW_SendSysDesc("N="SYSVIEW_APP_NAME",D="SYSVIEW_DEVICE_NAME",O=FreeRTOS");
  SEGGER_SYSVIEW_SendSysDesc("I#15=SysTick");  // 注册 SysTick 中断命名为 SysTick
}
```

### Step 4：修改 `FreeRTOSConfig.h` ★关键

```c
// 必须启用跟踪功能
#define configUSE_TRACE_FACILITY                 1

// 在文件末尾的 USER CODE BEGIN Defines 中添加：
/* USER CODE BEGIN Defines */
#include "SEGGER_SYSVIEW_FreeRTOS.h"  // 激活所有 trace 宏
#define INCLUDE_xTaskGetIdleTaskHandle 1
/* USER CODE END Defines */
```

> ⚠️ **为什么必须在末尾 include？**
> `SEGGER_SYSVIEW_FreeRTOS.h` 中定义了 `traceTASK_CREATE`、`traceTASK_SWITCHED_IN`、`traceISR_ENTER` 等宏，用于重写 FreeRTOS 的默认空 trace 宏。FreeRTOS 内核源文件（`tasks.c`、`queue.c`）通过 `FreeRTOS.h` → `FreeRTOSConfig.h` 链访问。必须放在末尾以确保所有配置项都已定义。

### Step 5：修改 `main.c` 初始化顺序 ★关键

```c
int main(void) {
    HAL_Init();
    SystemClock_Config();
    
    /* === SystemView 初始化必须在 SystemClock_Config() 之后 === */
    // 1. 使能 DWT Cycle Counter（时间戳源）
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
    
    // 2. 初始化 SystemView（传入时间戳频率和 CPU 频率）
    SEGGER_SYSVIEW_Conf();
    
    // 3. 初始化外设
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_ADC1_Init();
    // ...
    
    // 4. 创建任务
    osKernelInitialize();
    MX_FREERTOS_Init();
    
    // 5. 启动调度器
    osKernelStart();
}
```

> ⚠️ **为什么 SystemView 必须在时钟配置后初始化？**
> `SEGGER_SYSVIEW_Init()` 会传递 `configCPU_CLOCK_HZ`（即 `SystemCoreClock`）给 SystemView 用于时间戳转换。如果在时钟初始化前调用，`SystemCoreClock = 16000000`（His 默认），而实际 PLL 后为 100MHz，导致时间戳频率错误。

---

## 4. 关键配置详解

### 4.1 `SEGGER_SYSVIEW_Conf.h` 参数

| 参数 | 推荐值 | 说明 |
|------|--------|------|
| `SEGGER_SYSVIEW_START_ON_INIT` | `1` | 初始化后自动开始录制，避免手动调用 `Start()` |
| `SEGGER_SYSVIEW_RTT_CHANNEL` | `1` | RTT 通道号。`0`=auto 可能会与其他组件冲突 |
| `SEGGER_SYSVIEW_RTT_BUFFER_SIZE` | `4096` | 上游（目标→主机）缓冲。elog 等高流量组件共享 RTT 时建议加大 |
| `SEGGER_SYSVIEW_GET_TIMESTAMP` | DWT CYCCNT（默认）| Cortex-M3/M4 默认使用 DWT Cycle Counter |

### 4.2 `FreeRTOSConfig.h` 必须项

```c
#define configUSE_TRACE_FACILITY          1   // 启用 FreeRTOS 跟踪设施
#define INCLUDE_xTaskGetIdleTaskHandle    1   // 准确识别空闲任务
// + #include "SEGGER_SYSVIEW_FreeRTOS.h"  // 在文件末尾
```

### 4.3 可选优化项

```c
#define configUSE_STATS_FORMATTING_FUNCTIONS   1  // 启用运行时统计
#define configGENERATE_RUN_TIME_STATS          1  // 生成运行时间统计
```

---

## 5. 常见坑点与解决方案

### 坑点 1：SystemView 连不上 / "Could not find RTT buffer"

**根因矩阵**：

| # | 原因 | 现象 | 修复 |
|---|------|------|------|
| 1 | DWT Cycle Counter 未使能 | 时间戳为 0 或恒定 | `CoreDebug->DEMCR \|= TRCENA` + `DWT->CTRL \|= CYCCNTENA` |
| 2 | `SEGGER_SYSVIEW_Conf()` 在时钟配置前调用 | 时间戳频率错误 | 移到 `SystemClock_Config()` 之后 |
| 3 | `SYSVIEW_RAM_BASE` 错误 | 找不到 RAM 中的缓存 | STM32F411 SRAM = `0x20000000`（非 `0x10000000` CCM）|
| 4 | `SEGGER_SYSVIEW_Conf.h` 为空/未配置 | 初始化不完整 | 至少设置 `START_ON_INIT=1` |
| 5 | **elog 的 `SEGGER_RTT_Init()` 冲掉 RTT 控制块** | 最常见！通道忽隐忽现 | **注释掉 elog_port.c 中的 `SEGGER_RTT_Init()`** |

### 坑点 5 详解：elog 与 SystemView RTT 冲突

**问题**：elog 初始化时调用 `SEGGER_RTT_Init()` → `_DoInit()` → `memset(&_SEGGER_RTT, 0, sizeof)`。这会清零整个 RTT 控制块，销毁 SystemView 已创建的通道。

**调用链**：

```
main()
  └─ HAL_Init()
  └─ SystemClock_Config()
  └─ DWT 使能
  └─ SEGGER_SYSVIEW_Conf()           ← 创建 SysView RTT 通道
  └─ osKernelStart()
       └─ StartDefaultTask()
            └─ app_elog_init()
                 └─ elog_init()
                      └─ elog_port_init()
                           └─ SEGGER_RTT_Init()  ← memset 清零！冲掉 SystemView 通道
```

**修复**：注释掉 `elog_port.c` 中的 `SEGGER_RTT_Init()` 调用。RTT 已在 `SEGGER_SYSVIEW_Conf()` 中初始化完成，无需重复调用。

### 坑点 6：SystemView 连接了但无任务/中断信息

**根因**：`FreeRTOSConfig.h` 末尾缺少 `#include "SEGGER_SYSVIEW_FreeRTOS.h"`。

**效果**：SystemView 能连接（RTT 通信正常），但收不到任何 RTOS 事件（任务创建/切换、中断等），因为 trace 宏全部为空默认值。

**修复**：详见 Step 4。

### 坑点 7：SystemView 事件丢失 / 只有部分任务可见

**根因**：

- `SEGGER_SYSVIEW_RTT_BUFFER_SIZE` 过小（默认 1024）
- elog 通过 RTT 输出大量日志（~200KB/s），挤占 SystemView 事件包带宽

**现象**：

- SystemView 状态栏出现 **"Lost Events"** 计数
- 某个执行时间短的任务完全不可见
- Timeline 显示不连续

**修复**：

1. 增大缓冲到 4096+
2. 将 SystemView 和 elog 分配到不同 RTT 通道（Channel 0 = elog/Terminal, Channel 1 = SystemView）
3. 长期方案：将 elog 输出切换到 UART，完全释放 RTT 带宽

---

## 6. 使用指南

### 6.1 连接流程

```
1. 编译烧录 → 确保目标板运行
2. 打开 SEGGER SystemView PC 软件
3. 选择 Target Interface: SWD
4. 选择 Device: STM32F411CE
5. Target Interface Speed: 4000kHz (或 Auto)
6. 点击 "Start Recording"
```

### 6.2 界面导读

| 区域 | 功能 |
|------|------|
| **Timeline** | 时间轴视图，显示各任务/中断的执行时段 |
| **Task List** | 任务列表，显示名称、优先级、栈使用率、CPU 负载 |
| **Events** | 原始事件日志，包含队列操作、API 调用等 |
| **CPU Load** | CPU 总负载及各任务占比 |

### 6.3 关键操作

| 操作 | 方法 |
|------|------|
| 缩放时间轴 | 鼠标滚轮 / Ctrl+ 滚轮 |
| 平移 | 拖拽 |
| 查看任务详情 | 点击任务条 |
| 过滤事件 | 右侧 Events 面板过滤 |
| 导出录制 | File → Save Recording (.psf) |
| 标记时刻 | 录制中点 Flag 按钮 |

### 6.4 录制模式

- **One-shot**：录制固定时长后停止，适合分析启动过程
- **Continuous**：持续录制，循环缓冲，适合长时间监控
- **On-demand**：点在目标板上触发事件时录制

---

## 7. RAM/Flash 开销

| 项目 | 大小 | 说明 |
|------|------|------|
| Code | ~1.5KB | SEGGER_SYSVIEW.c + SEGGER_SYSVIEW_FreeRTOS.c |
| RO-data | ~0.1KB | 字符串常量 |
| ZI-data | 4096+ | RTT 缓冲（由 `SEGGER_SYSVIEW_RTT_BUFFER_SIZE` 决定）|
| **合计** | ~5.7KB Flash + 4KB+ RAM | |

---

## 8. 故障排除速查表

| 现象    | 检查项                        | 修复                                                  |
| ----- | -------------------------- | --------------------------------------------------- |
| 连不上   | DWT 使能了？                   | `CoreDebug->DEMCR \|= TRCENA_Msk`                   |
| 连不上   | 时钟初始化顺序？                   | `SEGGER_SYSVIEW_Conf()` 在 `SystemClock_Config()` 之后 |
| 连不上   | RAM Base 正确？               | STM32F411: `0x20000000`                             |
| 连不上   | RTT 控制块被冲？                 | 注释 elog 的 `SEGGER_RTT_Init()`                       |
| 无任务信息 | `FreeRTOSConfig.h` include | 末尾加 `#include "SEGGER_SYSVIEW_FreeRTOS.h"`          |
| 无任务信息 | `configUSE_TRACE_FACILITY` | 设为 1                                                |
| 事件丢失  | RTT 缓冲太小                   | 增到 4096+，分离通道                                       |
| 时间戳异常 | DWT 使能？                    | 检查初始化代码                                             |
| 时间戳异常 | 时钟频率传递                     | `SystemClock_Config()` 之后调用                         |
| 栈溢出   | 任务栈大小                      | 检查 `uxTaskGetStackHighWaterMark()`                  |

---

## 9. 参考

- [SEGGER SystemView 官方文档](https://www.segger.com/products/development-tools/systemview/)
- [SystemView FreeRTOS 集成说明](https://www.segger.com/products/development-tools/systemview/technology/rtos-support/)

---

## 变更记录

| 日期 | 内容 |
|------|------|
| 2026-06-07 | 初始版本，基于 STM32F411CEU6 + FreeRTOS V10.3.1 移植经验 |

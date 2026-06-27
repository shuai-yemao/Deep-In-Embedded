# 项目: 16_ADC_DMA_M_Arch_ope | 问题: SEGGER SystemView 无法找到 RTT 控制块

> 创建时间: 2026-06-06 21:38
> MCU: STM32F411CEU6

# 一、问题的描述

## 1. 问题的表现是怎样的？

SystemView PC 软件连接 J-Link 后提示「无法找到sysview缓存数组」，无法接收跟踪数据

## 2. 问题的复现路径

1) 编译烧录当前工程 2) 打开 SEGGER SystemView PC 软件 3) 选择 J-Link 连接 STM32F411CEU6 4) 点击 Start Recording → 弹出错误

## 3. 正常的预期是什么？

SystemView 应正常识别 RTT 控制块并显示 RTOS 任务调度、中断等事件

# 二、问题产生的可能原因分析

## 1. 初步 checklist 确认

- [ ] 0. 排除硬件问题：跑已知正常固件验证硬件
- [ ] 1. 程序爆栈：调整启动文件栈大小或 RTOS 任务栈
- [ ] 2. 过度优化：降低优化等级至 -O0
- [ ] 3. 死循环/HardFault：调试模式暂停查看 PC/LR，栈回溯
- [ ] 4. 执行错误：打印每个相关函数的返回值
- [ ] 5. 空指针：打断点检查指针是否为 0x00000000
- [ ] 6. API 用错：RTOS 用原生 API 而非 CMSIS wrapper
- [ ] 7. 未执行到：关键分支放 printf 标记
- [ ] 8. 线程饿死：加 vTaskDelay(100)
- [ ] 9. 无 while(1)：检查线程是否有死循环
- [ ] 10. 死锁：依次关闭互斥量/信号量排查
- [ ] 11. 局部变量未赋初值

## 2. 提出可能的假设

1) DWT Cycle Counter 未使能导致时间戳始终为 0
2) SEGGER_SYSVIEW_Conf() 在 SystemClock_Config() 之前调用，传递 16MHz 而非 100MHz 时间戳频率
3) SEGGER_SYSVIEW_Conf.h 为空，关键配置依赖默认值
4) 未调用 SEGGER_SYSVIEW_Start()
5) SYSVIEW_RAM_BASE=0x10000000 错误，应为 0x20000000
6) 双份 SEGGER_RTT 文件潜在冲突

# 三、设计实验，验证可能的原因和猜想

修复方案：
A) 在 SystemClock_Config() 之后增加 DWT 使能：CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk
B) 将 SEGGER_SYSVIEW_Conf() 移到 SystemClock_Config() 之后
C) SEGGER_SYSVIEW_Conf.h 增加 DWT 相关配置
D) 修正 SYSVIEW_RAM_BASE 为 0x20000000

# 四、验证实验

> 实验记录将在后续通过 `record_issue.py append` 追加到此区域。


## 第1次实验

### 1. 实验时间

2026-06-06 21:45

### 2. 实验环境

#### 1. 本次测试环境

- MCU: (待补充)
- 固件版本: (待补充)
- 电源: (待补充)

#### 2. 相关文档

(待补充)

#### 3. 实验步骤

(待补充)

#### 4. 实验结果

##### 4.1 输出结果

已实施修复：
1) main.c: 移除 line 74 的 SEGGER_SYSVIEW_Conf()，改在 SystemClock_Config() 后调用，并在调用前使能 DWT Cycle Counter
2) SEGGER_SYSVIEW_Config_FreeRTOS.c: SYSVIEW_RAM_BASE 从 0x10000000 修正为 0x20000000
3) SEGGER_SYSVIEW_Conf.h: 从空文件填充为包含 SEGGER_SYSVIEW_START_ON_INIT=1 等配置
4) 删除 Middlewares/Third_Party/RTT/ 目录，工程改为引用 Systemview/Src/下的 SEGGER_RTT.c
注：缺乏 Keil MDK 命令行工具，本机无法编译验证，需在 Keil MDK IDE 中编译确认

##### 4.2 实验分析

1. 与前次实验步骤对比: (待补充)
2. 与前次实验结果对比: (待补充)



## 第2次实验

### 1. 实验时间

2026-06-06 21:55

### 2. 实验环境

#### 1. 本次测试环境

- MCU: (待补充)
- 固件版本: (待补充)
- 电源: (待补充)

#### 2. 相关文档

(待补充)

#### 3. 实验步骤

(待补充)

#### 4. 实验结果

##### 4.1 输出结果

编译 & 烧录通过。48 warnings 全部消除 → 0 Error, 0 Warning。
额外修复：
- freertos.c: 添加 #include <string.h> (line 31)；删除未使用的变量 queue2_peek_data (原 line 217)
- elog_port.c: 3个非void函数添加 return 语句 (lines 124-139)；文件末尾添加换行符

##### 4.2 实验分析

1. 与前次实验步骤对比: (待补充)
2. 与前次实验结果对比: (待补充)



## 第3次实验

### 1. 实验时间

2026-06-06 22:02

### 2. 实验环境

#### 1. 本次测试环境

- MCU: (待补充)
- 固件版本: (待补充)
- 电源: (待补充)

#### 2. 相关文档

(待补充)

#### 3. 实验步骤

(待补充)

#### 4. 实验结果

##### 4.1 输出结果

第2轮修复：发现 elog_port_init() 中 SEGGER_RTT_Init() 强制 memset 清零整个 RTT CB，销毁了 SysView 通道。已注释掉该调用，保留 SEGGER_SYSVIEW_Conf() 中的 RTT 初始化。
编译 0 Error 0 Warning，烧录成功，等待 SystemView PC 连接验证。

##### 4.2 实验分析

1. 与前次实验步骤对比: (待补充)
2. 与前次实验结果对比: (待补充)


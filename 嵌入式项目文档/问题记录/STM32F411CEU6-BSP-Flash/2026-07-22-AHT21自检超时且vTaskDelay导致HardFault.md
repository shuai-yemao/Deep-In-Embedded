# 项目: STM32F411CEU6-BSP-Flash | 问题: AHT21自检超时且vTaskDelay导致HardFault

> 创建时间: 2026-07-22 18:08
> MCU: STM32F411CEU6

# 一、问题的描述

## 1. 问题的表现是怎样的？

AHT21 自检超时：RTT 输出 'self-test: timeout', AHT21 test: FAIL。取消注释 vTaskDelay(pdMS_TO_TICKS(500)) 后直接进入 HardFault_Handler。

## 2. 问题的复现路径

1. (待补充：工程文件 + 复现细节)

## 3. 正常的预期是什么？

AHT21 自检测试 PASS，RTT 输出温湿度数值，如 `temp=25.50 C, humi=45.20 %`。

## 2. 问题的复现路径

1. 工程: `30_stm32f411ceu6_bsp_flash_platform`，分支 `bsp_flash_platform`
2. 编译烧录后，RTT 输出显示 `AHT21 test: FAIL`
3. 在 `drv_adapter_temp_humi.c:744` 取消注释 `vTaskDelay(pdMS_TO_TICKS(500))`
4. 重新编译烧录 → 直接进入 HardFault_Handler

# 二、问题产生的可能原因分析

## 1. 初步 checklist 确认

- [ ] 0. 排除硬件问题：跑已知正常固件验证硬件
- [✓] 1. 程序爆栈：调整启动文件栈大小或 RTOS 任务栈
- [ ] 2. 过度优化：降低优化等级至 -O0
- [ ] 3. 死循环/HardFault：调试模式暂停查看 PC/LR，栈回溯
- [ ] 4. 执行错误：打印每个相关函数的返回值
- [ ] 5. 空指针：打断点检查指针是否为 0x00000000
- [ ] 6. API 用错：RTOS 用原生 API 而非 CMSIS wrapper
- [ ] 7. 未执行到：关键分支放 printf 标记
- [✓] 8. 线程饿死：加 vTaskDelay(100)
- [ ] 9. 无 while(1)：检查线程是否有死循环
- [ ] 10. 死锁：依次关闭互斥量/信号量排查
- [✓] 11. 局部变量未赋初值

## 2. 提出可能的假设

### 根因 1：vTaskDelay 被注释 → Handler 线程饿死

`drv_adapter_temp_humi.c:744`:
```c
// vTaskDelay(pdMS_TO_TICKS(500));  // ← 被注释！
```

调用链：defaultTask (osPriorityNormal) → 发送事件到队列 → **立即检查** self_test_done (=0) → 超时返回。Handler 线程 (优先级=1) 从未被调度。

### 根因 2：defaultTask 栈溢出 → HardFault

`freertos.c:54`:
```c
.stack_size = 128 * 4,   // 仅 512 字节！
```

取消注释 vTaskDelay 后，FreeRTOS 上下文切换时需保存：
| 内容 | 大小 |
|------|------|
| 核心寄存器 (R0-R12, SP, LR, PC, xPSR) | ~64 bytes |
| FPU 寄存器 (S0-S31 + FPSCR) | ~132 bytes |
| **上下文保存合计** | **~200 bytes** |

叠加调用链 `StartDefaultTask → system_init_all → system_test_aht21 → temp_humi_adapter_self_test` 的栈使用（~300-350B） + elog 格式化开销 → 超过 512B → **栈溢出 → HardFault**。

### 辅助因子：DEBUG_AHT21=1

`bsp_aht21_driver.h:94`: `DEBUG_AHT21 1` 导致 AHT21 驱动的 `elog_e/elog_i` 宏在 Handler 线程上下文中被执行，而 Handler 线程栈注释明确警告 "线程栈不足以支撑 vsnprintf→vfprintf"。

# 三、设计实验，验证可能的原因和猜想

1. `defaultTask` 栈从 128*4 增至 **256*4**（512→1024 字节）
2. 取消注释 `vTaskDelay(pdMS_TO_TICKS(500))`
3. `DEBUG_AHT21` 从 1 改为 **0**，防止 Handler 线程中调用 elog
4. 编译烧录后观察 RTT 输出

### 修改文件清单

| 文件 | 修改 |
|------|------|
| `Core/Src/freertos.c:54` | `.stack_size = 128 * 4` → `256 * 4` |
| `Bsp/Platform/drv_adapter_temp_humi/Src/drv_adapter_temp_humi.c:744` | 取消注释 `vTaskDelay` |
| `Bsp/AHT21/hal_driver/Inc/bsp_aht21_driver.h:94` | `DEBUG_AHT21 1` → `0` |

# 四、验证实验

### 1. 实验时间
2026-07-22

### 2. 实验环境
- 芯片: STM32F411CEU6
- 调试器: J-Link V9
- RTOS: FreeRTOS (CMSIS v2)
- 传感器: AHT21 (I2C addr=0x38, PB6=SCL, PB7=SDA)

### 3. 实验结果
待重新编译烧录后验证。

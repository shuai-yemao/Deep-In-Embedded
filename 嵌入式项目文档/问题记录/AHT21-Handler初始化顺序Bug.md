# 一、问题的描述

## 1. 问题的表示是怎样的？

AHT21 温湿度 handler 测试线程发送读取事件后，handler 线程收不到事件，日志显示：

```
I/TEMP_HUMI       [0.114] request: queue put OK
I/TEMP_HUMI       [0.116] thread: request received
E/TEMP_HUMI       [0.116] thread: measure failed, status=3
```

队列通信正常（put OK + received），但测量返回 status=3（`TEMP_HUMI_ERRORRESOURCE`）。

关键警告日志：
```
W/TEMP_HUMI       [0.112] register: handler not initialized
```

## 2. 问题的复现路径

1. 工程基于 STM32F411CEU6 + FreeRTOS (CMSIS-RTOSv2) + SEGGER RTT
2. AHT21 传感器通过软件 I2C（PB6/PB7）连接
3. `StartDefaultTask()` → `drv_adapter_port_temp_humi_test()` → 创建测试线程
4. 测试线程调用 `bsp_temp_humi_inst()` → 内部 `bsp_temp_humi_init()` → 创建传感器 → 创建队列 → 创建 handler 线程
5. 测试线程进入循环，每 2 秒调用 `bsp_temp_humi_handler_request()` 发送异步事件
6. Handler 线程收到事件后调用 `bsp_read_temp_humi()` 失败

## 3. 正常的预期是什么？

- 传感器注册成功，`instance_num > 0`
- `bsp_read_temp_humi()` 调用传感器驱动成功读取温湿度
- RTT 输出 `Temp=xx.xx C, Humi=xx.xx %`

# 二、问题产生的可能原因分析

## 1. 初步 checklist

- AHT21 传感器检测正常 ✅（`detect: addr=0x38, status=0x18`）
- 驱动实例初始化成功 ✅（`instantiate: success`）
- 队列通信正常 ✅（`request: queue put OK` + `thread: request received`）
- 传感器注册失败 ❌（`register: handler not initialized`）

## 2. 根因分析

初始化流程时序问题：

```
bsp_temp_humi_inst()
  → self->pf_init(self)          // bsp_temp_humi_init()
    → 创建传感器实例（调用 aht21_pf_create）
    → 调用 bsp_temp_humi_instance_register()
      → 检查 self->is_inited == TEMP_HUMI_INITED
      → ❌ is_inited 还是 TEMP_HUMI_NOT_INITED！失败，instance_num=0
    → 创建队列
    → 创建 handler 线程
    → 返回
  → self->is_inited = TEMP_HUMI_INITED  // ← 设置太晚了！
```

`bsp_temp_humi_instance_register()` 检查 `self->is_inited == TEMP_HUMI_INITED`，但 `is_inited` 由 `bsp_temp_humi_inst()` 在 `pf_init` 返回后才设置。传感器注册在 `pf_init` 内部进行，此时 `is_inited` 为 `TEMP_HUMI_NOT_INITED`，注册静默失败。

后续 `bsp_read_temp_humi()` 检查 `if (0 == self->temp_humi_instance.instance_num)` 返回 `TEMP_HUMI_ERRORRESOURCE`（status=3）。

### 次要问题：Handler 线程栈溢出

修复前 Handler 线程只有 256 words（1024 bytes），调用链：
```
handler_entry → queue_get → bsp_read_temp_humi → pf_measure
  → aht21_measure → aht21_read_measurement → I2C send/receive → elog
```

多层函数指针间接调用 + float 运算 + elog 格式化导致栈溢出，CPU 进入 HardFault。

# 三、修复方案

## 修复 1：提前设置 is_inited

在 `bsp_temp_humi_init()` 中，传感器注册循环**之前**设置 `self->is_inited = TEMP_HUMI_INITED`：

```c
/* 清空实例数组... */
self->is_inited = TEMP_HUMI_INITED;  // ← 提前设置

/* 遍历 g_sensor_table，创建并注册传感器 */
for (uint32_t i = 0; g_sensor_table[i] != NULL; i++) {
    ...
    self->pf_instance_register(self, instance, ops);  // ← 现在能通过了
}
```

并在队列/线程创建失败的 error path 中恢复 `self->is_inited = TEMP_HUMI_NOT_INITED`。

## 修复 2：增大栈空间

- Handler 线程栈：256 → **512 words**（`bsp_temp_humi_handler.c`）
- 测试线程栈：1024 → **2048 bytes**（`drv_adapter_port_temp_humi.c`）
- 启用 `configCHECK_FOR_STACK_OVERFLOW 1`

## 修复 3：删除未使用的 `freertos_app.c`

该文件中的 `vApplicationStackOverflowHook` 已移至 `freertos.c`。

# 四、验证

烧录后 RTT 日志：

```
I/PORT            [0.000] handler inst starting...
I/PORT            [0.000] AHT21 driver inst...
I/AHT21           [0.112] detect: addr=0x38, status=0x18
I/AHT21           [0.112] instantiate: success
I/PORT            [0.112] AHT21 inst OK
I/TEMP_HUMI       [0.112] init: sensor 'AHT21' registered   ← ✅ 注册成功
I/TEMP_HUMI       [0.113] init: success
I/TEMP_HUMI       [0.113] instantiate: success
I/TEMP_HUMI       [0.114] request: queue put OK
I/TEMP_HUMI       [0.116] thread: request received
I/TEMP_HUMI       [0.116] thread: measure OK, temp=xx.xx, humi=xx.xx
I/PORT            [0.116] Temp=xx.xx C, Humi=xx.xx %        ← ✅
```

# 五、经验总结

| 问题 | 根因 | 修复 |
|------|------|------|
| sensor 注册失败 | `is_inited` 在注册后才设置 | 提前设置 `is_inited` |
| Handler 栈溢出 | 256 words 不够深层调用链 | 增加到 512 words |
| HardFault 无提示 | 未启用栈溢出检测 | `configCHECK_FOR_STACK_OVERFLOW 1` |
| 无效 `freertos_app.c` | 遗留未使用的文件 | 删除，功能移至 `freertos.c` |

## 初始化顺序要点

Handler 结构体内部的初始化标志必须**在引用它的子函数被调用之前**设置，顺序为：

```
1. 清空实例数组
2. 设置 is_inited = TEMP_HUMI_INITED    ← 必须在此
3. 注册传感器（调用 pf_instance_register） ← 然后才能注册
4. 创建队列
5. 创建 handler 线程
6. 返回
```

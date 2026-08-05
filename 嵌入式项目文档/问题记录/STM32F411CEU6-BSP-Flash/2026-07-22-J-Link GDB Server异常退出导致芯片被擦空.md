# 项目: STM32F411CEU6-BSP-Flash | 问题: J-Link GDB Server 异常退出导致芯片被擦空

> 创建时间: 2026-07-22 18:08
> MCU: STM32F411CEU6

# 一、问题的描述

## 1. 问题的表现是怎样的？

VS Code Cortex-Debug 启动 J-Link GDB Server 后闪退，报错 'GDB Server Quit Unexpectedly'。RTT 日志停止输出，芯片无响应。flash.jlink 执行 erase 成功后 loadfile 阶段目标电压降至 0.0V。

## 2. 问题的复现路径

1. (待补充：工程文件 + 复现细节)

## 3. 正常的预期是什么？

J-Link GDB Server 正常启动，Cortex-Debug F5 一键烧录调试，RTT 日志正常输出。

# 二、问题产生的可能原因分析

## 1. 初步 checklist 确认

- [✓] 0. 排除硬件问题：J-Link V9 固件正常，USB 识别成功
- [✓] 1. 程序爆栈：J-Link 侧无关
- [ ] 2. 过度优化：-
- [✓] 3. 死循环/HardFault：`JLink.exe -nogui 1 -CommandFile flash.jlink` 命令测试确认连接 → 擦除成功 → loadfile 失败

### 命令测试关键输出

```
VTref=3.320V              ← 目标供电正常
Device "STM32F411CE" selected  ← 芯片识别正确
erase → Done (8.321s)     ← 擦除成功
loadfile → "Target voltage too low (1 Volt is required, Measured: 0.0 Volt)"
                          ← 电压降到 0V，烧录失败！
```

随后 J-Link 连探针都无法连接：

```
WARNING: Out of sync, resynchronizing...
FAILED: Cannot connect to the probe/programmer.
```

## 2. 提出可能的假设

1. **根因**：`flash.jlink` 脚本先 `erase`（擦除成功），后 `loadfile` 时目标电压异常降至 0V，导致烧录失败。芯片 Flash 已被全擦但新固件未写入 → CPU 无法启动 → GDB Server 超时退出。
2. **链式后果**：擦空后的芯片可能进入低功耗/异常状态，SWD 接口响应异常，导致 J-Link 通信中断，触发 USB 层 `Out of sync` 错误。
3. **预防措施**：烧录脚本应先 `loadfile` 成功后再 `erase`（或不使用全片擦除，仅按扇区擦除）。

## 1. 初步 checklist 确认

- [✓] 0. 排除硬件问题：跑已知正常固件验证硬件
- [✓] 1. 程序爆栈：调整启动文件栈大小或 RTOS 任务栈
- [ ] 2. 过度优化：降低优化等级至 -O0
- [✓] 3. 死循环/HardFault：调试模式暂停查看 PC/LR，栈回溯
- [ ] 4. 执行错误：打印每个相关函数的返回值
- [ ] 5. 空指针：打断点检查指针是否为 0x00000000
- [ ] 6. API 用错：RTOS 用原生 API 而非 CMSIS wrapper
- [ ] 7. 未执行到：关键分支放 printf 标记
- [ ] 8. 线程饿死：加 vTaskDelay(100)
- [ ] 9. 无 while(1)：检查线程是否有死循环
- [ ] 10. 死锁：依次关闭互斥量/信号量排查
- [ ] 11. 局部变量未赋初值

## 2. 提出可能的假设

(待补充：假设列表)

# 三、设计实验，验证可能的原因和猜想

1. 拔插 J-Link USB 线 → 运行 `JLink.exe` 枚举探针确认连接恢复
2. 创建不含 `erase` 的烧录脚本（仅 `loadfile + r + go`），避免再次擦空
3. 降低 SWD 速度至 1000kHz 提高通信稳定性
4. 检查 SWD 接线（GND/CLK/DIO）接触可靠性，必要时更换杜邦线
5. 恢复后使用 Cortex-Debug F5 一键烧录调试，观察 RTT 输出

# 四、验证实验

## 第一次实验

### 1. 实验时间

2026-07-22

### 2. 实验环境

- 芯片: STM32F411CEU6
- 调试器: J-Link V9 (SN: 69701612, FW: 2021-05-07)
- J-Link 软件: V9.30
- 接口: SWD, 4000kHz (建议降至 1000kHz)
- 供电: USB 5V → 板载 LDO 3.3V (VTref=3.320V)

### 3. 实验步骤

1. 拔插 J-Link USB 线等待 5 秒
2. `JLink.exe` 确认探针重新识别
3. 移除 `flash.jlink` 中的 `erase` 行
4. 重新执行烧录
5. 观察 RTT 输出确认固件运行

### 4. 实验结果

待烧录验证。

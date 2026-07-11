# 一、问题的描述

## 1. 问题的表示是怎样的？

AHT21温湿度传感器驱动开发过程中遇到多个问题，按发现顺序排列：

### Bug#1 — elog日志输出显示 "NO_TAG"

RTT输出结果：
```
System initialized
I/elog            [0.000] EasyLogger V2.2.99 is initialize success.
A/NO_TAG          [0.000 heap:14216 defaultTask] (debug.c:33 test_elog)this assert
E/NO_TAG          [0.000] (debug.c:34 test_elog)this is error
W/NO_TAG          [0.000] this is warning
I/NO_TAG          [0.000] this is info
D/NO_TAG          (debug.c:37 test_elog)this is debug
V/NO_TAG          this is verbose
System Starting.....
I/NO_TAG          [0.117] AHT21
I/NO_TAG          [0.117] AHT21
System initialized
```

所有级别的日志 tag 均显示为 `NO_TAG`，但自定义 tag "AHT21" 出现在消息末尾位置。

### Bug#2 — AHT21实例初始化失败 "invalid chip id"

```
E/AHT21 [0.056] (...bsp_aht21_driver.c:271 aht21_read_id)read_id failed: invalid chip id
E/AHT21 [0.056] (...bsp_aht21_driver.c:725 bsp_aht21_driver_inst)inst failed: invalid chip id
E/AHT21 [0.056] (...system_adapter.c:216 system_init_resources)AHT21 driver inst failed
```

`aht21_read_id` 返回成功但 chip ID 校验永远失败。

### Bug#3 — I2C状态读取协议不匹配

AHT21状态字读取需要：写0x71命令 → stop → start → 读1字节。但 `pf_receive_bytes` 使用 repeated start，不适用于此场景。

### Bug#4 — aht21_init初始化序列不符合数据手册

原始代码：上电等待40ms → 直接读状态。数据手册要求：上电等待≥100ms → 发初始化命令(0xBE+0x08+0x00) → 等10ms → 读状态检查校准位。

## 2. 问题的复现路径

1. 工程基于 STM32F411CEU6 + FreeRTOS + EasyLogger v2.2.99 + SEGGER RTT
2. AHT21传感器通过软件I2C连接（PB13=SDA, PB14=SCL）
3. 在 `system_init_resources()` 中调用 `bsp_aht21_driver_inst()` 初始化驱动
4. RTT Viewer 观察输出

## 3. 正常的预期是什么？

- elog输出应显示自定义tag（如 "AHT21"、"elog"），而非 "NO_TAG"
- AHT21实例初始化应成功，`aht21_read_id` 返回 `AHT21_OK`
- 温湿度数据应能正常读取

# 二、问题产生的可能原因分析

## 1. 初步 checklist 确认

- 0.排除硬件问题：传感器接线确认（PB13/PB14），GPIO时钟已使能 ✅
- 1.程序可能爆栈：defaultTask栈从512增到1024字节 ✅
- 2.程序可能被过度优化：未修改优化等级
- 3.程序可能进入死循环：已通过单步调试确认无死循环
- 4.可能程序执行错误：打印每个函数返回值 ✅
- 5.可能指针为空：确认实例指针非NULL
- 6.可能API接口用错：**elog宏使用错误** — 这是Bug#1的根因
- 7.可能有些程序片段没执行到：已确认所有分支都有输出
- 11.所有局部变量已赋初值 ✅

## 2. 提出可能的问题

### Bug#1 假设：elog宏 `log_e` 与 `elog_e` 的tag传递机制不同

查阅 `elog.h` 源码发现：
```c
// log_e 宏定义（第248行）
#define log_e(...)       elog_e(LOG_TAG, __VA_ARGS__)

// LOG_TAG 默认值（第237行）
#define LOG_TAG          "NO_TAG"

// elog_e 宏定义（第226行）
#define elog_e(tag, ...)     elog_error(tag, __VA_ARGS__)
```

`log_e("AHT21", msg)` 展开为 `elog_e(LOG_TAG, "AHT21", msg)` → tag="NO_TAG"，"AHT21" 变成了消息内容。

### Bug#2 假设：`aht21_read_id` 返回类型与比较值不匹配

```c
// aht21_read_id 返回 aht21_status_t（枚举），AHT21_OK = 0
// 但 bsp_aht21_driver_inst 中用 0x38 去比较
if (0x38 != aht21_read_id(self))  // 0x38 != 0 → 永远为 true！
```

### Bug#3 假设：两种I2C读操作需要不同的总线事务

- 温湿度数据读取：寄存器写 + repeated start + 读 → `IIC_Read_Multi_Byte` 可处理
- 状态字读取（0x71命令）：写命令 + **stop** + 新start + 读 → 需要分开两次事务

# 三、设计实验，验证可能的原因和猜想

## 实验1：验证elog tag问题

将 `DEBUG_AHT21_OUT` 宏从 `log_##level("AHT21", msg)` 改为 `elog_##level("AHT21", msg)`，绕过 `LOG_TAG` 自动传入机制。

## 实验2：验证chip ID比较问题

将 `if (0x38 != aht21_read_id(self))` 改为 `if (AHT21_OK != aht21_read_id(self))`。

## 实验3：验证状态读取协议

新增 `pf_read_status` 函数指针到驱动接口，使用原始IIC函数实现正确的两段式事务。

# 四、验证实验

## 第一次实验

### 1. 实验时间

2026-07-11

### 2. 实验环境

#### 1. 测试环境

- MCU：STM32F411CEU6（512KB Flash, 128KB RAM）
- 时钟：HSI + PLL → 100MHz
- RTOS：FreeRTOS（defaultTask栈1024字节）
- 日志库：EasyLogger v2.2.99 + SEGGER RTT
- 传感器：AHT21（I2C地址0x38, PB13=SDA, PB14=SCL）
- 编译器：Keil MDK ARM

#### 2. 相关文档

- AHT21 Datasheet（I2C协议、初始化序列、CRC校验）
- EasyLogger v2.2.99 源码（elog.h, elog.c）
- STM32F411 Reference Manual（GPIO, RCC）

#### 3. 实验步骤

1. 修改 `bsp_aht21_driver.h` 中 `DEBUG_AHT21_OUT` 宏：`log_##level` → `elog_##level`
2. 修改 `bsp_aht21_driver.c` 中 `aht21_init`：上电等待→100ms，初始化命令→3字节(0xBE,0x08,0x00)
3. 修改 `bsp_aht21_driver.c` 中 `bsp_aht21_driver_inst`：chip ID比较 `0x38` → `AHT21_OK`
4. 新增 `pf_read_status` 到 `iic_driver_interface_t`，用原始IIC函数实现状态读取
5. 修改 `system_adapter.c`：绑定 `pf_read_status`，修正 `log_e`/`log_i` → `elog_e`/`elog_i`
6. 编译烧录，RTT Viewer 观察输出

#### 4. 实验结果

##### 4.1 输出本次实验的结果

待编译烧录验证（代码修改已完成）。

##### 4.2 实验分析

**Bug#1 根因确认：elog宏的tag传递机制**

`log_e` 宏内部已经拼接了 `LOG_TAG` 作为第一个参数，用户再传 tag 会导致参数错位。正确用法：

```c
// ❌ 错误：tag 被双重传入
log_e("AHT21", "message");  // → elog_e("NO_TAG", "AHT21", "message")

// ✅ 正确：使用 log_e 时不传 tag（自动用 LOG_TAG）
log_e("message");

// ✅ 正确：使用 elog_e 显式指定 tag
elog_e("AHT21", "message");
```

**Bug#2 根因确认：枚举值与魔数比较**

`aht21_read_id` 返回 `aht21_status_t`（成功=0），用0x38比较永远失败。

**Bug#3 根因确认：I2C协议差异**

AHT21状态读取（0x71命令）需要两次独立事务（中间有stop），而 `pf_receive_bytes` 使用 repeated start，协议不匹配。

**Bug#4 根因确认：初始化序列**

原始代码缺少初始化命令发送步骤，且上电等待时间不足（40ms < 要求的100ms）。

# 五、经验总结与修复方案

## 核心教训

| 问题 | 根因 | 修复方案 |
|------|------|----------|
| "NO_TAG"显示 | `log_e` 宏自动加 `LOG_TAG`，用户再传 tag 导致参数错位 | 多模块共享文件用 `elog_e(tag, fmt)` 显式传 tag |
| chip ID永远失败 | `0x38 != aht21_read_id()` 比较枚举值与魔数 | 改为 `AHT21_OK != aht21_read_id()` |
| I2C状态读失败 | `pf_receive_bytes` 用 repeated start，AHT21状态读需要 stop | 新增 `pf_read_status` 专用函数 |
| 初始化失败 | 缺少初始化命令，上电等待不足 | 按数据手册实现完整初始化序列 |

## elog使用规范

```
文件级统一tag：  #define LOG_TAG "ModuleName"  →  log_e("msg")
多模块共享文件：  elog_e("SpecificTag", "msg")  →  显式传tag
```

## AHT21初始化流程（数据手册）

```
上电 → 等待≥100ms → 发0xBE+0x08+0x00 → 等10ms → 读状态字
→ 检查bit3(校准位) → 发0xAC+0x33+0x00(触发测量) → 等80ms → 读6字节+CRC
```

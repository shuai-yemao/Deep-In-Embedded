---
title: Unity 嵌入式使用手册
date: 2026-07-14
tags:
  - 嵌入式
  - Unity
  - STM32
  - 单元测试
  - Mock
  - 回归测试
aliases:
  - Unity 使用方法
---

# Unity 嵌入式使用手册

> [!summary] 这份手册解决什么问题
> 说明 Unity 移植完成后，如何在嵌入式 C 工程中编写单元测试、隔离硬件依赖、运行测试并进行日常回归。

本文只讲“如何使用 Unity”。官方源码获取、目录移植、Keil 配置和输出适配请看：

[[Unity嵌入式移植指南]]

## 1. Unity 在嵌入式项目中的定位

Unity 是一个面向 C 语言的轻量级测试框架，核心由 `unity.c`、`unity.h` 和 `unity_internals.h` 组成，适合加入 MCU 工程或主机测试工程。官方项目明确将嵌入式工具链作为主要使用场景之一。

Unity 负责三件事：

1. 提供 `TEST_ASSERT_*` 断言。
2. 运行 `RUN_TEST(test_xxx)` 指定的测试函数。
3. 统计通过、失败、忽略数量，并通过 `UNITY_END()` 返回结果。

Unity 不负责：

- 自动模拟 I2C、SPI、UART 或传感器。
- 自动创建测试任务。
- 自动替代硬件驱动。
- 自动决定测试应该在主机还是 STM32 上运行。

因此，嵌入式使用 Unity 的关键不是“把测试文件加入工程”，而是先给业务代码划分可替换的依赖边界。

## 2. 第一个可运行测试

### 2.1 测试文件基本结构

当前工程测试文件：

~~~text
Middlewares/Third_Party/Unity/Test/unity_port_smoke_test.c
~~~

最小结构如下：

~~~c
#include "unity.h"

void setUp(void)
{
    /* 每个测试开始前执行。 */
}

void tearDown(void)
{
    /* 每个测试结束后执行。 */
}

static void test_integer(void)
{
    TEST_ASSERT_EQUAL_INT(42, 40 + 2);
}

int unity_test_run(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_integer);
    return UNITY_END();
}
~~~

当前工程使用 `unity_test_run()`，而不是默认直接定义 `main()`，因为正式工程已经有 `Core/Src/main.c`。这样测试源文件可以参与正式 Target 编译而不会产生 `main multiply defined`。

只有独立测试 Target 才使用：

~~~c
#ifdef UNITY_TEST_MAIN
int main(void)
{
    return unity_test_run();
}
#endif
~~~

### 2.2 测试生命周期

Unity 每次执行一个测试时，顺序是：

~~~text
setUp()
    -> test_xxx()
    -> tearDown()
    -> 下一个测试
~~~

适合放入 `setUp()` 的内容：

- 清零测试状态结构体。
- 初始化假的传感器数据。
- 设置 Mock 函数的默认返回值。
- 初始化测试计数器。

不适合放入 `setUp()` 的内容：

- 启动真实 FreeRTOS 调度器。
- 初始化真实 I2C 外设。
- 依赖上一个测试留下的全局状态。
- 连接一次后供所有测试共享的隐式硬件状态。

每个测试应该能够单独运行，不能依赖测试执行顺序。

## 3. 常用断言

### 3.1 整数、布尔值和状态码

~~~c
TEST_ASSERT_TRUE(condition);
TEST_ASSERT_FALSE(condition);
TEST_ASSERT_EQUAL_INT(expected, actual);
TEST_ASSERT_NOT_EQUAL_INT(expected, actual);
TEST_ASSERT_EQUAL_UINT(expected, actual);
TEST_ASSERT_EQUAL_HEX8(expected, actual);
~~~

测试驱动返回值时，优先验证“状态码”和“输出数据”两部分：

~~~c
static void test_driver_returns_success_and_value(void)
{
    int status = sensor_read(&value);

    TEST_ASSERT_EQUAL_INT(SENSOR_OK, status);
    TEST_ASSERT_EQUAL_UINT(2500U, value);
}
~~~

### 3.2 字符串、数组和指针

~~~c
TEST_ASSERT_EQUAL_STRING("AHT21", sensor_name());
TEST_ASSERT_EQUAL_MEMORY(expected, actual, sizeof(expected));
TEST_ASSERT_NOT_NULL(buffer);
TEST_ASSERT_NULL(error_pointer);
~~~

### 3.3 浮点数

浮点数不能直接依赖完全相等，使用精度范围：

~~~c
TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.00f, measured_temperature);
~~~

### 3.4 错误路径

不要只测试正常路径，还要测试：

~~~c
static void test_sensor_timeout_returns_error(void)
{
    fake_i2c_set_timeout(true);

    TEST_ASSERT_EQUAL_INT(SENSOR_TIMEOUT, sensor_read(&value));
}
~~~

建议每个驱动或业务模块至少覆盖：成功、参数错误、超时、重试失败、边界值和恢复路径。

## 4. 测试对象分层

嵌入式项目不应把所有测试都放到 STM32 实物上。推荐按下面的层次分工：

| 层次 | 测试对象 | 依赖 | 推荐运行位置 |
|---|---|---|---|
| 纯函数 | CRC、温湿度换算、数据解析 | 无硬件 | 主机 GCC |
| 业务逻辑 | 阈值、告警、状态转换 | Fake 数据 | 主机 GCC |
| 驱动接口 | I2C 调用参数、重试次数 | Mock/Stub | 主机 GCC |
| 系统接口 | FreeRTOS 队列、任务协作 | RTOS 测试桩或目标板 | 专用测试 Target |
| 硬件验证 | 真实 AHT21 读数、时序 | 真实板卡 | STM32 目标板 |

ThrowTheSwitch 的构建建议也强调：单元测试通常应该在更快、更可控的环境运行；目标板更适合做系统级和硬件验证，而不是承载所有单元测试。

## 5. 如何隔离硬件依赖

### 5.1 推荐的依赖边界

把业务代码依赖的硬件操作收敛到接口中：

~~~c
typedef struct
{
    int (*write)(uint8_t address, const uint8_t *data, uint16_t length);
    int (*read)(uint8_t address, uint8_t *data, uint16_t length);
    void (*delay_ms)(uint32_t ms);
} sensor_bus_t;

int aht21_read(sensor_bus_t *bus, float *temperature, float *humidity);
~~~

正式工程传入真实 I2C 实现，测试工程传入 Fake 实现。这样测试 AHT21 解析逻辑时，不需要真的连接 AHT21。

### 5.2 最小 Fake 示例

~~~c
static int fake_read_status;
static uint8_t fake_read_data[8];
static unsigned int fake_read_count;

static int fake_i2c_read(uint8_t address, uint8_t *data, uint16_t length)
{
    (void)address;
    fake_read_count++;

    if (fake_read_status != 0) {
        return fake_read_status;
    }

    memcpy(data, fake_read_data, length);
    return 0;
}

static void test_aht21_retries_after_i2c_failure(void)
{
    sensor_bus_t bus = {
        .read = fake_i2c_read,
    };

    fake_read_status = -1;

    TEST_ASSERT_EQUAL_INT(AHT21_ERROR, aht21_read(&bus, &temperature, &humidity));
    TEST_ASSERT_EQUAL_UINT(3U, fake_read_count);
}
~~~

这个示例的重点是验证“失败后重试三次”，而不是验证 STM32 HAL 或 I2C 电平。HAL 和真实总线应在目标板测试中验证。

### 5.3 Mock 和 Stub 的区别

| 类型 | 作用 | 示例 |
|---|---|---|
| Stub | 返回预设数据，让被测代码继续执行 | I2C 返回固定温湿度帧 |
| Fake | 一个可运行的简化实现 | 内存版 Flash、环形队列 |
| Mock | 记录调用并验证交互 | 验证 I2C 被调用 3 次 |

项目规模较大时，可以在 Unity 基础上引入 CMock 或 Ceedling；它们分别用于生成 Mock 和组织测试构建。当前工程先使用手写 Fake，依赖少、便于在 Keil 和 GCC 两侧复用。

## 6. AHT21 测试建议

### 6.1 不建议直接这样测试

~~~c
static void test_aht21_real_sensor(void)
{
    IICInit(&AHT_bus);
    TEST_ASSERT_EQUAL_INT(0, AHT21_Read(&AHT_bus));
}
~~~

这个测试依赖真实传感器、电源、连线、时序和当前环境温度，更接近硬件验收测试，不是稳定的单元测试。

### 6.2 推荐拆成三类

1. **数据解析测试**：输入固定的 AHT21 原始 6 字节数据，验证温度和湿度换算。
2. **通信异常测试**：让 Fake I2C 返回 NACK、超时或 CRC 错误，验证错误码和重试次数。
3. **目标板验证测试**：在 STM32 上连接真实 AHT21，验证初始化、采样周期和长期运行。

### 6.3 测试命名建议

~~~text
test_aht21_parse_valid_frame
test_aht21_reject_invalid_crc
test_aht21_retry_after_i2c_timeout
test_aht21_convert_temperature_boundary
~~~

测试名应包含“对象 + 条件 + 预期结果”，这样 RTT 输出或 CI 报告中可以直接定位意图。

## 7. 当前工程的 elog/RTT 使用方式

当前工程定义 `UNITY_USE_ELOG` 时，输出链路为：

~~~text
Unity 输出字符
    -> unity_port.c 按行缓存
    -> elog_info("UNITY", line)
    -> elog_port_output()
    -> SEGGER_RTT_Write()
~~~

测试入口中使用工程现有日志初始化：

~~~c
#ifdef UNITY_USE_ELOG
#include "debug.h"
#endif

int unity_test_run(void)
{
#ifdef UNITY_USE_ELOG
    app_elog_init();
    test_elog();
    log_i("Unity test runner started");
#endif

    UNITY_BEGIN();
    RUN_TEST(test_aht21_parse_valid_frame);
    RUN_TEST(test_aht21_reject_invalid_crc);

    return UNITY_END();
}
~~~

RTT 中应看到类似：

~~~text
I/UNITY [0.003] test_aht21_parse_valid_frame:PASS
I/UNITY [0.003] test_aht21_reject_invalid_crc:PASS
I/UNITY [0.003] 2 Tests 0 Failures 0 Ignored
I/UNITY [0.003] OK
~~~

如果不使用 RTT，取消 `UNITY_USE_ELOG`，并确保工程已经将 `printf` 重定向到 UART、USB CDC 或其他输出通道。

## 8. 三种运行方式

### 8.1 主机 GCC 测试

适合纯函数、数据解析、业务逻辑和 Fake 驱动测试：

~~~powershell
gcc -std=c99 -Wall -Wextra -pedantic `
  -DUNITY_INCLUDE_CONFIG_H -DUNITY_TEST_MAIN `
  -IMiddlewares/Third_Party/Unity/Inc `
  Middlewares/Third_Party/Unity/Test/unity_port_smoke_test.c `
  Middlewares/Third_Party/Unity/Src/unity.c `
  Middlewares/Third_Party/Unity/Src/unity_port.c `
  -o unity-port-smoke.exe

.\unity-port-smoke.exe
~~~

返回值：

- `0`：所有测试通过。
- 非 `0`：至少一个测试失败或被测试框架判定为失败。

### 8.2 Keil 正式工程编译

可以把不包含 `main()` 的测试源文件加入正式 Target，验证测试代码本身能够通过 ARMCC 编译和链接。但这不会自动执行测试。

当前工程的 `unity_port_smoke_test.c` 默认只提供 `unity_test_run()`，因此不会与正式 `main.c` 冲突。

### 8.3 独立 STM32 测试 Target

适合验证 elog/RTT、FreeRTOS、真实外设和目标板行为：

1. 复制一个独立 Keil Target。
2. 保留 Unity、测试源和必要的业务源文件。
3. 移除正式应用 `main.c`，或让测试 Target 使用独立入口。
4. 定义 `UNITY_USE_ELOG`。
5. 在测试入口调用 `unity_test_run()`。
6. 烧录并通过 RTT Viewer 查看结果。

不要把独立测试 Target 的 `main()` 和正式应用 `main()` 同时链接。

## 9. 日常回归测试流程

每次修改驱动或业务逻辑后，建议按以下顺序执行：

~~~text
修改代码
  -> 主机 GCC 测试
  -> 检查失败测试和边界场景
  -> Keil 完整构建
  -> 必要时运行 STM32 目标板测试
  -> 查看 git diff 和测试输出
~~~

建议把测试分为三组：

- `fast`：主机上秒级完成的纯函数和 Fake 测试。
- `target`：需要 FreeRTOS、RTT 或 HAL 的目标板测试。
- `hardware`：需要真实 AHT21、传感器或外部设备的验收测试。

日常开发优先运行 `fast`；提交前运行 `fast + Keil build`；修改硬件驱动或通信时再运行 `target/hardware`。

## 10. 常见问题

| 现象 | 原因 | 处理 |
|---|---|---|
| `main multiply defined` | 测试文件和正式工程都定义了 `main()` | 使用 `unity_test_run()`；只有独立 Target 定义 `UNITY_TEST_MAIN` |
| `__ARM_use_no_argv multiply defined` | 通常由两个 `main()` 引起 | 先解决重复 `main()` |
| 测试能编译但没有执行 | 正式工程只编译了测试文件，没有调用测试入口 | 在专用测试任务中调用 `unity_test_run()` |
| RTT 中没有 Unity 输出 | elog 未初始化或 RTT 未启动 | 检查 `app_elog_init()`、`elog_start()` 和 J-Link RTT Viewer |
| Unity 输出没有标准 elog 前缀 | 使用了旧的逐字符 `elog_raw()` 适配 | 使用当前按行缓存的 `UnityOutputChar()` |
| 主机测试依赖 HAL 头文件 | 测试没有隔离硬件边界 | 为 I2C、延时和 GPIO 提供 Fake/Stub |
| 测试偶尔失败 | 依赖真实时间、任务调度或未清理的全局状态 | 在 `setUp()` 重置状态，固定输入和时序 |

## 11. 推荐的测试维护规则

- 一个测试函数只验证一个行为。
- 测试名写清条件和预期结果。
- 测试之间不共享可变状态。
- 不在单元测试中依赖真实传感器读数。
- 失败路径和边界值与成功路径同等重要。
- 测试代码也要经过编译器警告检查。
- 测试输出必须能够定位到测试名称。
- 修改接口时同步修改 Fake/Mock 和测试用例。
- 每次修复一个缺陷，至少增加一个回归测试。

## 12. 参考资料

本文的结构参考了以下资料，并结合当前 STM32 工程实际情况重写：

- [ThrowTheSwitch/Unity 官方仓库](https://github.com/ThrowTheSwitch/Unity)：源码、版本和嵌入式工具链定位。
- [ThrowTheSwitch：Which Build Method?](https://www.throwtheswitch.org/build/which)：主机、模拟器和目标板测试的取舍，以及硬件依赖隔离思路。
- [博客园：unity 单元测试](https://www.cnblogs.com/hxj568/p/17149939.html)：STM32 工程导入 Unity、添加源文件和测试入口的基本流程。
- [GitCode：Unity 单元测试框架：嵌入式 C 开发的轻量级测试解决方案](https://blog.gitcode.com/17936ea64883a0103606a5d6085e05e1.html)：轻量集成、Mock 外设和嵌入式测试价值的介绍。
- [CMake 下配置 Unity 测试框架](https://smhk.net/note/2024/11/setting-up-unity-test-framework-for-cmake/)：测试目录和独立测试构建的组织思路。

博客用于参考教程组织方式和实践经验；Unity API、源码和版本信息以官方仓库为准。

---
title: Unity 嵌入式使用手册
date: 2026-07-14
tags:
  - 嵌入式
  - Unity
  - STM32
  - 单元测试
aliases:
  - Unity 使用方法
---

# Unity 嵌入式使用手册

> [!summary] 适合谁阅读
> 这是一份给第一次接触 Unity C 单元测试的嵌入式开发者的入门手册。你不需要先学会 Mock、CMock 或复杂测试工具，先照着最小示例跑通一个测试即可。

本手册讲“移植完成以后怎么用 Unity”。如果你还没有把 Unity 放进工程，请先看：

[[Unity嵌入式移植指南]]

## 1. Unity 到底是什么

这里的 Unity 不是游戏引擎，而是一个用 C 语言写的“小测试工具”。它帮我们回答一个问题：

> 这段代码运行后，结果是不是我预期的结果？

比如我们有一段加法代码：

~~~c
int add(int a, int b)
{
    return a + b;
}
~~~

我们希望 `add(2, 3)` 得到 `5`。用 Unity 写成测试就是：

~~~c
TEST_ASSERT_EQUAL_INT(5, add(2, 3));
~~~

这句话可以读成：

> 我期望得到 5，请检查实际得到的结果是不是 5。

得到 5，测试通过；得到其他值，测试失败。

## 2. 先看一次完整测试

把下面代码保存成测试文件，就能理解 Unity 的基本用法：

~~~c
#include "unity.h"

void setUp(void)
{
    /* 每个测试开始前执行。暂时没有准备工作，可以留空。 */
}

void tearDown(void)
{
    /* 每个测试结束后执行。暂时没有清理工作，可以留空。 */
}

static void test_add_two_numbers(void)
{
    int result = 2 + 3;

    TEST_ASSERT_EQUAL_INT(5, result);
}

int unity_test_run(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_add_two_numbers);
    return UNITY_END();
}
~~~

这段代码分成四部分：

| 代码                                             | 白话解释          |
| ---------------------------------------------- | ------------- |
| `#include "unity.h"`                           | 使用 Unity 的功能  |
| `setUp()`                                      | 每个测试开始前做准备    |
| `test_add_two_numbers()`                       | 真正要检查的一件事     |
| `UNITY_BEGIN()` / `RUN_TEST()` / `UNITY_END()` | 开始、运行、结束并统计测试 |

### 2.1 为什么使用 `unity_test_run()` 而不是 `main()`

普通的 C 程序通常从 `main()` 开始。但你的 STM32 正式工程已经有一个 `main()`，如果测试文件再写一个 `main()`，链接时就会报：

~~~text
Symbol main multiply defined
~~~

所以当前工程使用：

~~~c
int unity_test_run(void)
{
    /* 运行 Unity 测试 */
}
~~~

正式工程可以编译这个测试函数；需要真正运行时，在测试任务或测试入口中调用 `unity_test_run()`。

如果是单独的主机测试程序，才额外打开：

~~~c
#ifdef UNITY_TEST_MAIN
int main(void)
{
    return unity_test_run();
}
#endif
~~~

## 3. 什么是“断言”

“断言”可以理解成考试时的答案检查器。

你先写出标准答案，再把程序实际算出的答案交给 Unity：

~~~c
int actual = add(2, 3);
TEST_ASSERT_EQUAL_INT(5, actual);
~~~

这里：

- `5` 是你认为正确的答案，叫“期望值”。
- `actual` 是程序实际算出的答案，叫“实际值”。
- 两者相同，测试通过。
- 两者不同，测试失败。

几乎所有 Unity 断言都可以用这个思路理解：

~~~text
准备数据 -> 调用函数 -> 得到结果 -> 检查结果
~~~

## 4. 最常用的断言

### 4.1 检查两个整数是否相等

~~~c
TEST_ASSERT_EQUAL_INT(期望值, 实际值);
~~~

例子：

~~~c
static void test_add(void)
{
    int actual = 2 + 3;

    TEST_ASSERT_EQUAL_INT(5, actual);
}
~~~

如果 `actual` 是 `5`，通过；如果是 `4`，失败。

常用于：

- 函数返回值
- 错误码
- 计数值
- 计算结果

### 4.2 检查两个整数不相等

~~~c
TEST_ASSERT_NOT_EQUAL_INT(期望不相等的值, 实际值);
~~~

例子：

~~~c
int result = sensor_read();
TEST_ASSERT_NOT_EQUAL_INT(SENSOR_ERROR, result);
~~~

意思是：这次读取结果不能是 `SENSOR_ERROR`。

### 4.3 检查条件是真的还是假的

~~~c
TEST_ASSERT_TRUE(条件);
TEST_ASSERT_FALSE(条件);
~~~

例子：

~~~c
TEST_ASSERT_TRUE(sensor_is_ready());
TEST_ASSERT_FALSE(sensor_is_busy());
~~~

可以这样理解：

- `TEST_ASSERT_TRUE(x)`：希望 `x` 为真。
- `TEST_ASSERT_FALSE(x)`：希望 `x` 为假。
- C 语言中，`0` 是假，非 `0` 是真。

如果你想检查一个具体错误码，建议使用 `TEST_ASSERT_EQUAL_INT()`，因为失败时更容易看出实际返回了什么。

### 4.4 检查字符串内容

~~~c
TEST_ASSERT_EQUAL_STRING(期望字符串, 实际字符串);
~~~

例子：

~~~c
const char *name = "AHT21";
TEST_ASSERT_EQUAL_STRING("AHT21", name);
~~~

它比较的是文字内容，不是两个地址是否相同。

不要写成：

~~~c
TEST_ASSERT_EQUAL_INT((int)"AHT21", (int)name);
~~~

这比较的是地址，不能正确判断两个字符串是否相同。

如果数据不是以 `\0` 结尾，而是一个固定长度的协议字段，可以使用：

~~~c
char code[4] = {'A', 'H', 'T', '2'};
TEST_ASSERT_EQUAL_STRING_LEN("AHT2", code, 4);
~~~

### 4.5 检查指针是否为空

~~~c
TEST_ASSERT_NULL(pointer);
TEST_ASSERT_NOT_NULL(pointer);
~~~

例子：

~~~c
void *buffer = create_buffer();
TEST_ASSERT_NOT_NULL(buffer);
~~~

意思是：创建缓冲区后，返回的地址不能是空地址。

错误处理时可以反过来写：

~~~c
void *buffer = create_buffer_when_memory_is_full();
TEST_ASSERT_NULL(buffer);
~~~

意思是：内存不足时，函数应该返回空地址。

注意：指针不为空，只能说明“有一个地址”，不能说明地址指向的数据一定正确。数据内容还要继续检查。

### 4.6 检查一段数据是否相同

~~~c
TEST_ASSERT_EQUAL_MEMORY(期望地址, 实际地址, 字节数);
~~~

检查协议数据时常用：

~~~c
uint8_t expected[] = {0xAC, 0x33, 0x00};
uint8_t actual[] = {0xAC, 0x33, 0x00};

TEST_ASSERT_EQUAL_MEMORY(expected, actual, 3);
~~~

意思是：从两个地址开始，连续比较 3 个字节。

如果想让失败日志更容易看懂，可以使用十六进制数组断言：

~~~c
TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, actual, 3);
~~~

这里的 `3` 是元素数量，必须与数组实际长度匹配。

### 4.7 检查浮点数

温度、湿度等计算结果通常不能要求完全相等，因为浮点计算可能产生很小的误差。

~~~c
TEST_ASSERT_FLOAT_WITHIN(允许误差, 期望值, 实际值);
~~~

例子：

~~~c
float actual_temperature = 25.004f;

TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.00f, actual_temperature);
~~~

它实际检查的是：

~~~text
实际值与期望值的差的绝对值 <= 允许误差
~~~

这里误差是 `0.004`，小于 `0.01`，所以通过。

### 4.8 检查大于、小于和范围

~~~c
TEST_ASSERT_GREATER_THAN(下限, 实际值);
TEST_ASSERT_LESS_THAN(上限, 实际值);
TEST_ASSERT_INT_WITHIN(允许误差, 期望值, 实际值);
~~~

例如检查电池电压不能低于 3.0 V：

~~~c
float voltage = read_battery_voltage();
TEST_ASSERT_GREATER_THAN_FLOAT(3.0f, voltage);
~~~

例如检查温度大约是 25 度：

~~~c
TEST_ASSERT_INT_WITHIN(1, 25, temperature);
~~~

它允许温度范围为 `24` 到 `26`。

### 4.9 检查某一位是否为 1

嵌入式代码经常把很多开关放在一个寄存器里。例如 bit3 表示传感器准备好了：

~~~c
uint8_t status = 0x08;

TEST_ASSERT_BIT_HIGH(3, status);
~~~

意思是：检查 `status` 的第 3 位是不是 1。

检查某一位必须为 0 时：

~~~c
TEST_ASSERT_BIT_LOW(3, status);
~~~

## 5. 一条测试应该怎样写

推荐固定使用下面三步：

### 第一步：准备输入

~~~c
fake_i2c_load_valid_frame();
~~~

### 第二步：调用被测函数

~~~c
status = aht21_read(&fake_bus, &temperature, &humidity);
~~~

### 第三步：检查结果

~~~c
TEST_ASSERT_EQUAL_INT(AHT21_OK, status);
TEST_ASSERT_FLOAT_WITHIN(0.1f, 25.0f, temperature);
TEST_ASSERT_FLOAT_WITHIN(0.1f, 50.0f, humidity);
~~~

完整示例：

~~~c
static void test_aht21_read_success(void)
{
    float temperature = 0.0f;
    float humidity = 0.0f;
    int status;

    fake_i2c_load_valid_frame();
    status = aht21_read(&fake_bus, &temperature, &humidity);

    TEST_ASSERT_EQUAL_INT(AHT21_OK, status);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 25.0f, temperature);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 50.0f, humidity);
}
~~~

## 6. `setUp()` 和 `tearDown()` 是做什么的

可以把每个测试想象成一次独立实验：

- `setUp()`：实验前摆好器材。
- 测试函数：进行实验并检查结果。
- `tearDown()`：实验后收拾器材。

例如每次测试前都把 Fake I2C 恢复为正常状态：

~~~c
void setUp(void)
{
    fake_i2c_reset();
}

void tearDown(void)
{
    fake_i2c_reset();
}
~~~

这样一个测试失败，也不会把错误状态带给下一个测试。

## 7. AHT21 应该怎么测试

不要一开始就让每个测试都连接真实 AHT21。真实传感器会受到接线、电源、时序和环境温度影响，测试容易一会儿通过、一会儿失败。

先把测试分成三种：

### 7.1 不接硬件的测试

给程序一组固定的原始数据，检查温湿度换算是否正确：

~~~c
static void test_aht21_convert_data(void)
{
    float temperature;
    float humidity;

    aht21_convert_raw(known_raw_data, &temperature, &humidity);

    TEST_ASSERT_FLOAT_WITHIN(0.1f, 25.0f, temperature);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 50.0f, humidity);
}
~~~

这类测试可以在电脑上快速运行。

### 7.2 模拟通信失败

“模拟”就是测试时故意让假的 I2C 返回错误：

~~~c
static void test_aht21_retry_after_timeout(void)
{
    fake_i2c_return_timeout();

    TEST_ASSERT_EQUAL_INT(
        AHT21_TIMEOUT,
        aht21_read(&fake_bus, &temperature, &humidity));
}
~~~

这样可以检查超时、重试和错误处理，而不需要真的拔掉 I2C 线。

### 7.3 连接真实硬件测试

最后再把测试程序烧到 STM32，连接真实 AHT21，检查：

- I2C 是否能正常通信。
- 传感器初始化是否成功。
- 连续读取是否稳定。
- FreeRTOS 任务中是否能正常运行。

这属于硬件测试，不要把它和电脑上的快速单元测试混为一谈。

## 8. RTT、elog 和 printf 输出

当前工程使用的输出路径是：

~~~text
Unity 测试文字
    -> Unity 适配层
    -> elog
    -> RTT
    -> RTT Viewer 窗口
~~~

测试入口可以这样写：

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
    RUN_TEST(test_aht21_convert_data);
    return UNITY_END();
}
~~~

RTT 中会看到类似：

~~~text
I/UNITY [0.003] test_aht21_convert_data:PASS
I/UNITY [0.003] 1 Tests 0 Failures 0 Ignored
I/UNITY [0.003] OK
~~~

如果没有 RTT：

1. 不定义 `UNITY_USE_ELOG`。
2. 保留 Unity 的 `printf` 输出分支。
3. 确保工程已经把 `printf` 重定向到 UART 或 USB CDC。

## 9. 测试怎样运行

### 9.1 在电脑上运行

适合测试加法、数据解析、温湿度换算和假的 I2C：

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

看到 `OK`，说明测试全部通过。看到 `FAIL`，先看失败测试的文件名和行号。

### 9.2 在 Keil 中编译

测试文件可以加入 Keil 工程编译，但不能再定义一个 `main()`。当前工程使用 `unity_test_run()`，因此不会与正式 `main.c` 冲突。

### 9.3 在 STM32 上运行

需要建立独立测试 Target，或者在专门的测试任务中调用 `unity_test_run()`。不要把正式应用的 `main()` 和测试程序的 `main()` 同时加入一个 Target。

## 10. 初学者最容易遇到的问题

| 现象 | 用白话说原因 | 怎么处理 |
|---|---|---|
| `main multiply defined` | 工程里有两个程序入口 | 测试文件改用 `unity_test_run()` |
| 只看到编译成功，没有测试输出 | 只是把测试文件编译了，没有调用它 | 在测试入口调用 `unity_test_run()` |
| RTT 没有输出 | elog 或 RTT 还没启动 | 先调用 `app_elog_init()`，再打开 RTT Viewer |
| 浮点测试失败 | 两个小数存在计算误差 | 使用 `TEST_ASSERT_FLOAT_WITHIN()` |
| 字符串测试失败 | 实际字符串多了字符或没有 `\0` | 检查字符串长度和结束符 |
| 测试一会儿通过一会儿失败 | 测试之间互相影响，或依赖真实硬件 | 在 `setUp()` 重置状态，使用固定输入 |
| 主机上通过，板子上失败 | 主机没有验证真实硬件 | 增加 STM32 目标板测试 |

## 11. 记住这五句话

1. Unity 就是帮你检查“实际结果是不是预期结果”的工具。
2. 一条测试通常是：准备输入、调用函数、检查结果。
3. `expected` 写期望结果，`actual` 写程序实际结果。
4. 纯计算和数据解析优先在电脑上测试，真实传感器放到 STM32 上测试。
5. 正式工程已有 `main()` 时，测试入口使用 `unity_test_run()`。

## 12. 继续学习

- [Unity 官方仓库](https://github.com/ThrowTheSwitch/Unity)
- [Unity 官方入门指南](https://github.com/ThrowTheSwitch/Unity/blob/v2.6.1/docs/UnityGettingStartedGuide.md)
- [Unity 官方断言说明](https://github.com/ThrowTheSwitch/Unity/blob/v2.6.1/docs/UnityAssertionsReference.md)
- [博客园：STM32 使用 Unity 单元测试](https://www.cnblogs.com/hxj568/p/17149939.html)

如果某个断言、代码或错误日志看不懂，可以把具体测试代码和 RTT/Keil 输出贴出来，按“输入 → 调用 → 结果 → 断言”的顺序一起分析。

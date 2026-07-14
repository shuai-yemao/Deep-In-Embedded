---
title: Unity C 单元测试框架移植与使用指南
date: 2026-07-14
tags:
  - 嵌入式
  - 中间件
  - Unity
  - STM32
  - Keil
  - 单元测试
aliases:
  - Unity 嵌入式移植
---

# Unity C 单元测试框架移植与使用指南

> [!summary] 这篇指南解决什么问题
> 把 Unity C 单元测试框架加入 STM32F411CEU6 + Keil 工程，并说明源码来源、文件位置、Keil 配置、测试用例写法和验证方法。

## 0. 先看结论

本工程已经完成以下集成：

- 官方 Unity v2.6.1 源码已放入 Middlewares/Third_Party/Unity。
- Keil 工程已加入 Unity 头文件路径、unity.c 和 unity_port.c。
- 已加入可执行冒烟测试 unity_port_smoke_test.c。
- GCC 冒烟测试通过。
- Keil 完整构建通过：0 Error(s), 0 Warning(s)。
- Unity 输出适配为 Unity → elog → SEGGER RTT，复用工程现有日志链路。
- 尚未在 STM32 实物上烧录并通过 UART/RTT 观察测试输出。

“框架集成成功”不等于“AHT21 业务代码已经完成单元测试”。

## 1. Unity 是什么

这里的 Unity 不是游戏引擎，而是 ThrowTheSwitch 提供的 C 语言单元测试框架。

| 文件 | 作用 |
|---|---|
| unity.c | 测试运行器、断言实现、结果统计 |
| unity.h | TEST_ASSERT_* 等对外测试宏 |
| unity_internals.h | 内部类型、底层宏和运行接口 |
| unity_config.h | 目标平台的编译和输出配置 |
| unity_port.c | 本工程适配层，提供弱 setUp/tearDown |

基本运行链路：

~~~text
UNITY_BEGIN()
    -> RUN_TEST(test_xxx)
    -> UNITY_END()
    -> 返回 0 表示通过，非 0 表示失败
~~~

目标板上的日志输出链路：

~~~text
Unity 的单字符输出
    -> UnityOutputChar()
    -> elog_raw()
    -> elog_port_output()
    -> SEGGER_RTT_Write()
    -> J-Link RTT Viewer
~~~

Unity 不直接依赖 RTT；启用 `UNITY_USE_ELOG` 时由工程现有的 elog port 统一管理。

未定义 `UNITY_USE_ELOG` 时，适配层自动退回 `printf`。因此目标工程只要已经把 `printf` 重定向到 UART、USB CDC 或其他控制台，就可以在没有 RTT 的情况下查看 Unity 输出。

## 2. 官方源码地址

本次使用官方仓库的 v2.6.1 标签，不使用本地缓存或自定义兼容实现。

- 官方仓库：[ThrowTheSwitch/Unity](https://github.com/ThrowTheSwitch/Unity)
- 版本目录：[Unity v2.6.1](https://github.com/ThrowTheSwitch/Unity/tree/v2.6.1)
- 官方 unity.c：[src/unity.c](https://github.com/ThrowTheSwitch/Unity/blob/v2.6.1/src/unity.c)
- 官方 unity.h：[src/unity.h](https://github.com/ThrowTheSwitch/Unity/blob/v2.6.1/src/unity.h)
- 官方 unity_internals.h：[src/unity_internals.h](https://github.com/ThrowTheSwitch/Unity/blob/v2.6.1/src/unity_internals.h)
- 官方配置模板：[examples/unity_config.h](https://github.com/ThrowTheSwitch/Unity/blob/v2.6.1/examples/unity_config.h)
- 官方许可证：[LICENSE.txt](https://github.com/ThrowTheSwitch/Unity/blob/v2.6.1/LICENSE.txt)

官方文件标识：

| 官方文件 | Git blob SHA |
|---|---|
| src/unity.c | 7fa2dc6306607f24148c40f9f4751c906d4a2a6f |
| src/unity.h | 9e2a97b791a56bf073bea5d8835575becdb863f9 |
| src/unity_internals.h | 562f5b6da6d05a736423c58847a26acba1c0ef0c |

## 3. 本工程目录结构

~~~text
STM32F411CEU6_AHT21/
├── Middlewares/Third_Party/Unity/
│   ├── Inc/
│   │   ├── unity.h
│   │   ├── unity_internals.h
│   │   └── unity_config.h
│   ├── Src/
│   │   ├── unity.c
│   │   └── unity_port.c
│   ├── Test/
│   │   └── unity_port_smoke_test.c
│   └── LICENSE.txt
└── MDK-ARM/STM32F411CEU6_AHT21.uvprojx
~~~

- Inc 和 Src：框架及平台适配代码。
- Test：测试代码，不混入正式业务入口。
- Keil 工程：只登记正式 target 需要参与编译的 Unity 源文件。

## 4. 从零开始的移植步骤

### Step 1：锁定并获取官方源码

只需要获取：

~~~text
src/unity.c
src/unity.h
src/unity_internals.h
examples/unity_config.h
LICENSE.txt
~~~

本工程锁定仓库 ThrowTheSwitch/Unity 的 v2.6.1 标签。

### Step 2：创建中间件目录

~~~text
Middlewares/Third_Party/Unity/
├── Inc
├── Src
└── Test
~~~

官方头文件放进 Inc，官方 unity.c 放进 Src。

### Step 3：配置 unity_config.h

Unity 在嵌入式环境中主要需要：

1. 编译器定义 UNITY_INCLUDE_CONFIG_H。
2. 定义 UNITY_OUTPUT_CHAR，决定测试结果输出位置。

当前工程的 `unity_config.h` 使用一个适配函数：

~~~c
void UnityOutputChar(int character);
#define UNITY_OUTPUT_CHAR(ch) UnityOutputChar(ch)
~~~

`unity_port.c` 再把字符交给 elog：

~~~c
#ifdef UNITY_USE_ELOG
#include "elog.h"
#else
#include <stdio.h>
#endif

void UnityOutputChar(int character)
{
#ifdef UNITY_USE_ELOG
    elog_raw("%c", (char)character);
#else
    printf("%c", (char)character);
#endif
}
~~~

这里不能直接在 Unity 适配层调用 `SEGGER_RTT_Write`，否则会绕过工程统一的日志格式、输出锁和 elog port。

如果工程没有 RTT，则不要定义 `UNITY_USE_ELOG`，并确保已有 `printf` 重定向，例如：

~~~c
int fputc(int ch, FILE *f)
{
    (void)f;
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}
~~~

上面的 UART 示例只说明输出条件；实际串口句柄应替换为工程中已经初始化的句柄。

### Step 4：配置 Keil

在 Options for Target → C/C++ → Include Paths 增加：

~~~text
..\Middlewares\Third_Party\Unity\Inc
~~~

在 Define 中增加：

~~~text
UNITY_INCLUDE_CONFIG_H
UNITY_USE_ELOG
~~~

对应工程文件：

~~~text
MDK-ARM/STM32F411CEU6_AHT21.uvprojx
~~~

### Step 5：把源码登记到 Keil

新增分组 Middlewares/Unity，并加入：

~~~text
..\Middlewares\Third_Party\Unity\Src\unity.c
..\Middlewares\Third_Party\Unity\Src\unity_port.c
~~~

官方 Unity 会引用 setUp 和 tearDown。不提供它们会导致：

~~~text
Undefined symbol setUp
Undefined symbol tearDown
~~~

unity_port.c 提供弱定义；测试文件可以提供强定义覆盖它们。

### Step 6：加入第一个测试文件

文件：

~~~text
Middlewares/Third_Party/Unity/Test/unity_port_smoke_test.c
~~~

最小结构：

~~~c
#include "unity.h"

#ifdef UNITY_USE_ELOG
#include "debug.h"
#endif

void setUp(void) {}
void tearDown(void) {}

static void test_integer(void)
{
    TEST_ASSERT_EQUAL_INT(42, 40 + 2);
}

int unity_test_run(void)
{
#ifdef UNITY_USE_ELOG
    app_elog_init();
    test_elog();
    log_i("Unity test runner started");
#endif

    UNITY_BEGIN();
    RUN_TEST(test_integer);
    {
        int result = UNITY_END();
#ifdef UNITY_USE_ELOG
        log_i("Unity test runner finished: %s", result == 0 ? "PASS" : "FAIL");
#endif
        return result;
    }
}

#ifdef UNITY_TEST_MAIN
int main(void)
{
    return unity_test_run();
}
#endif
~~~

其中 `app_elog_init()` 和 `test_elog()` 对应工程现有的 `User/Debug/Src/debug.c` 实现。正式测试 target 启用 `UNITY_USE_ELOG` 时，Unity 输出和测试过程日志会共用 elog/RTT；主机 GCC 冒烟测试不定义该宏，因此不会依赖 STM32 的 elog、FreeRTOS 或 RTT。

注意：正式工程已经有 `Core/Src/main.c`，所以测试文件默认只提供 `unity_test_run()`，不能直接再定义 `main()`。独立 Unity 测试 target 才定义 `UNITY_TEST_MAIN`，让该文件生成自己的测试入口。

不希望测试文件定义 main 时，可以改用 unity_main，由独立测试任务或测试 target 调用。

## 5. 如何编写测试

### 5.1 整数和布尔值

~~~c
TEST_ASSERT_TRUE(condition);
TEST_ASSERT_FALSE(condition);
TEST_ASSERT_EQUAL_INT(expected, actual);
TEST_ASSERT_EQUAL_UINT(expected, actual);
TEST_ASSERT_NOT_EQUAL_INT(expected, actual);
~~~

### 5.2 字符串

~~~c
static void test_sensor_name(void)
{
    TEST_ASSERT_EQUAL_STRING("AHT21", "AHT21");
}
~~~

### 5.3 指针和空值

~~~c
static void test_buffer_pointer(void)
{
    uint8_t buffer[8];

    TEST_ASSERT_NOT_NULL(buffer);
    TEST_ASSERT_NULL(NULL);
}
~~~

### 5.4 失败测试

~~~c
TEST_FAIL_MESSAGE("driver returned an unexpected status");
~~~

测试失败后，UNITY_END 返回非零值。

## 6. 测试对象如何选择

| 层次    | 测试对象              | 推荐验证方式        |
| ----- | ----------------- | ------------- |
| 纯函数层  | CRC、温湿度换算、数据解析    | 主机 GCC        |
| 状态机层  | 初始化、重试、超时状态       | 主机 GCC + stub |
| 驱动接口层 | I2C 调用参数和次数       | mock/stub     |
| 硬件层   | 实际 AHT21 读数       | STM32 目标板     |
| 系统层   | FreeRTOS、UART、RTT | 目标板或专用测试固件    |

本次 smoke test 只验证 Unity 框架本身，不代表 AHT21 驱动已经完成单元测试。

## 7. 如何运行测试

### 7.1 主机 GCC 冒烟测试

~~~powershell
gcc -std=c99 -Wall -Wextra -pedantic -DUNITY_INCLUDE_CONFIG_H -DUNITY_TEST_MAIN -IMiddlewares/Third_Party/Unity/Inc Middlewares/Third_Party/Unity/Test/unity_port_smoke_test.c Middlewares/Third_Party/Unity/Src/unity.c Middlewares/Third_Party/Unity/Src/unity_port.c -o unity-port-smoke.exe
./unity-port-smoke.exe
~~~

返回值 0 表示通过，返回值 1 表示至少一个测试失败。

### 7.2 Keil 完整构建

~~~powershell
G:\keil5\core\UV4\UV4.exe -b MDK-ARM\STM32F411CEU6_AHT21.uvprojx -o build.log
~~~

日志应包含：

~~~text
0 Error(s), 0 Warning(s)
~~~

### 7.3 目标板测试

1. 先调用工程已有的 `app_elog_init()`，确保 `elog_init()` 和 `elog_start()` 已执行。
2. 建立独立 Unity Test Target，或提供 unity_main。
3. 烧录测试固件。
4. 通过 J-Link RTT Viewer 观察 Unity 测试输出。
5. 根据 `UNITY_END()` 返回值判断通过或失败。

测试入口的关键顺序是：

~~~c
app_elog_init();
UNITY_BEGIN();
RUN_TEST(test_xxx);
return UNITY_END();
~~~

## 8. 本次实际验证结果

| 项目 | 结果 |
|---|---|
| 官方 Unity v2.6.1 源码编译 | 通过 |
| Unity 冒烟测试 | 通过，UNITY_SMOKE_EXIT=0 |
| TDD 失败路径 | 故意修改断言后返回 RED_EXIT=1 |
| TDD 通过路径 | 恢复正确断言后返回 GREEN_EXIT=0 |
| Keil 完整构建 | 通过，0 Error(s), 0 Warning(s) |
| Keil XML 工程解析 | 通过 |
| Unity → elog → RTT 适配编译 | 通过 |
| STM32 实物 UART/RTT 测试 | 尚未执行 |

## 9. 常见问题

| 现象 | 原因 | 处理 |
|---|---|---|
| Cannot open source input file unity.h | include path 未配置 | 增加 Unity/Inc |
| Undefined symbol setUp | 没有生命周期函数 | 加入 unity_port.c |
| Undefined symbol tearDown | 没有生命周期函数 | 加入 unity_port.c |
| 链接到 semihosting 或 putchar | 默认输出走标准输出 | 使用 UnityOutputChar 适配函数 |
| main 重复定义 | 测试和正式固件共用 target | 独立测试 target 或改用 unity_main |
| 能编译但看不到测试结果 | elog 未初始化、RTT 未启动、输出被过滤，或 printf 未重定向 | RTT 路径检查 app_elog_init、ELOG_OUTPUT_ENABLE 和 RTT Viewer；printf 路径检查 fputc/_write 重定向 |
| 主机通过但硬件失败 | 主机未验证硬件时序 | 增加目标板测试 |
| 测试源未参与构建 | 未加入测试 target | 检查 Keil Project 分组 |

## 10. Git 修改范围

~~~text
MDK-ARM/STM32F411CEU6_AHT21.uvprojx
Middlewares/Third_Party/Unity/Inc/unity.h
Middlewares/Third_Party/Unity/Inc/unity_internals.h
Middlewares/Third_Party/Unity/Inc/unity_config.h
Middlewares/Third_Party/Unity/Src/unity.c
Middlewares/Third_Party/Unity/Src/unity_port.c
Middlewares/Third_Party/Unity/Test/unity_port_smoke_test.c
Middlewares/Third_Party/Unity/LICENSE.txt
~~~

Unity 适配层只新增 `UnityOutputChar()`；`elog_port.c` 和 RTT 仍复用工程现有实现。

查看差异：

~~~powershell
git status --short
git diff -- MDK-ARM/STM32F411CEU6_AHT21.uvprojx
git diff --check -- MDK-ARM/STM32F411CEU6_AHT21.uvprojx
~~~

本次适配层的关键差异如下：

~~~diff
-#define UNITY_OUTPUT_CHAR(ch) ((void)(ch))
+void UnityOutputChar(int character);
+#define UNITY_OUTPUT_CHAR(ch) UnityOutputChar(ch)

+#ifdef UNITY_USE_ELOG
+    elog_raw("%c", (char)character);
+#else
+    printf("%c", (char)character);
+#endif
~~~

因此切换方式只有一个工程宏：

| 场景 | `UNITY_USE_ELOG` | 输出链路 |
|---|---|---|
| 当前工程 | 定义 | Unity → elog → SEGGER RTT |
| 无 RTT 工程 | 不定义 | Unity → printf → UART/USB CDC 等重定向目标 |
~~~

## 11. 参考资料

本文结构参考公开教程常见的“下载源码 → 整合工程 → 处理 setUp/tearDown → 编写测试入口 → 运行验证”顺序：

- [博客园：unity 单元测试](https://www.cnblogs.com/hxj568/p/17149939.html)
- [Microchip：Add Unity Test Framework](https://onlinedocs.microchip.com/oxy/GUID-FDED4652-7F52-4069-A2F3-D654D6EF1FD3-en-US-3/GUID-33E0E764-2175-49E0-BA7A-28FF34CAEF22.html)
- [ThrowTheSwitch 官方 Unity 仓库](https://github.com/ThrowTheSwitch/Unity)
- [Unity Getting Started Guide](https://github.com/ThrowTheSwitch/Unity/blob/v2.6.1/docs/UnityGettingStartedGuide.md)

博客用于参考教程结构；源码、API 和版本信息以官方仓库为准。

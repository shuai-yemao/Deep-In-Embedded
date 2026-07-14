---
title: Unity 嵌入式移植指南
date: 2026-07-14
tags:
  - 嵌入式
  - 中间件
  - Unity
  - STM32
  - 单元测试
aliases:
  - Unity 移植
---

# Unity 嵌入式移植指南

## 1. 目标与范围

本次目标是在 `STM32F411CEU6_AHT21` 的 `Middlewares` 目录中加入 C 语言 Unity 单元测试框架的嵌入式入口，使驱动和业务模块可以使用 `TEST_ASSERT_*` 宏进行测试。

目标工程：STM32F411CEU6，Cortex-M4F，Keil ARMCC 5，现有 FreeRTOS。

Unity 官方项目：<https://github.com/ThrowTheSwitch/Unity>

本次实际采用官方仓库 `v2.6.1` 标签源码，不再使用自定义 Unity 兼容实现。

## 2. 目录约定

```text
Middlewares/Third_Party/Unity/
├── Inc/
│   ├── unity.h                 # 官方源码
│   ├── unity_config.h          # 官方 examples 配置模板 + 本工程输出宏
│   └── unity_internals.h       # 官方源码
└── Src/
    └── unity.c                 # 官方源码
```

`unity.c` 只应在测试构建中参与链接。若正式固件不包含测试入口，可从 Keil target 中排除 Unity 源文件，以避免增加固件体积。

官方文件校验标识：

| 文件 | 官方 Git blob SHA |
|---|---|
| `src/unity.c` | `7fa2dc6306607f24148c40f9f4751c906d4a2a6f` |
| `src/unity.h` | `9e2a97b791a56bf073bea5d8835575becdb863f9` |
| `src/unity_internals.h` | `562f5b6da6d05a736423c58847a26acba1c0ef0c` |
| `examples/unity_config.h` | `efd91232c5d41b97608c41ece2a68f5eb57807b3`（落盘后追加本工程输出宏） |

## 3. 移植要点

### 3.1 输出接口

`unity_config.h` 将 Unity 输出映射到 `UnityOutputChar()`。当前默认实现为空函数，保证库不会隐式依赖 semihosting 或 `printf`。实际测试时，应在应用或测试工程中替换该函数，将字符送到 UART、RTT 或日志中。

### 3.2 测试运行器

```c
#include "unity.h"

void test_aht21_status(void)
{
    TEST_ASSERT_TRUE(1);
}

void run_tests(void)
{
    UnityBegin("AHT21");
    UnityDefaultTestRun(test_aht21_status, "test_aht21_status", __LINE__);
    (void)UnityEnd();
}
```

建议测试代码放在单独的 `User/Tests` 或测试 target 中，不要直接在生产 `main()` 中长期调用。

## 4. 本次工程修改

### 4.1 初始修改

- [x] 从官方 `ThrowTheSwitch/Unity@v2.6.1` 获取 `unity.h`
- [x] 从官方 `ThrowTheSwitch/Unity@v2.6.1` 获取 `unity_internals.h`
- [x] 从官方 `ThrowTheSwitch/Unity@v2.6.1` 获取 `unity.c`
- [x] 从官方 `examples/unity_config.h` 获取配置模板并加入本工程输出宏
- [x] 从官方 `LICENSE.txt` 获取许可证文件
- [x] 将 Unity 头文件目录加入 Keil include path
- [x] 将 `unity.c` 加入 Keil 的 `Middlewares/Unity` 分组
- [ ] 在真实 AHT21 驱动测试中接入 UART/RTT 输出
- [ ] 用 Keil 完整编译并在目标板运行测试

## 5. Git diff 回填

以下内容由工程修改完成后实际命令回填。新文件当前未暂存，因此普通 `git diff` 不会显示其正文；状态中的 `??` 已保留，便于后续审查。

<!-- DIFF-BEGIN -->
```text
 M MDK-ARM/STM32F411CEU6_AHT21.uvprojx
?? Middlewares/Third_Party/Unity/

 MDK-ARM/STM32F411CEU6_AHT21.uvprojx | 12 +++++++++++-
 1 file changed, 11 insertions(+), 1 deletion(-)
```

实际已登记的工程差异：

```diff
@@ -340,7 +340,7 @@
-...;..\Middlewares\RTT\Inc;../System/Delay/Inc;...
+...;..\Middlewares\RTT\Inc;..\Middlewares\Third_Party\Unity\Inc;../System/Delay/Inc;...
@@ -1211,6 +1211,16 @@
+        <Group>
+          <GroupName>Middlewares/Unity</GroupName>
+          <Files>
+            <File>
+              <FileName>unity.c</FileName>
+              <FileType>1</FileType>
+              <FilePath>..\Middlewares\Third_Party\Unity\Src\unity.c</FilePath>
+            </File>
+          </Files>
+        </Group>
```

新增文件：

| 文件 | 用途 |
|---|---|
| `Middlewares/Third_Party/Unity/Inc/unity.h` | 对外测试宏和运行器接口 |
| `Middlewares/Third_Party/Unity/Inc/unity_config.h` | 嵌入式输出钩子 |
| `Middlewares/Third_Party/Unity/Inc/unity_internals.h` | 核心类型和断言声明 |
| `Middlewares/Third_Party/Unity/Src/unity.c` | 运行器、计数器和核心断言实现 |
<!-- DIFF-END -->

## 6. 验证结论

当前结论：官方 Unity v2.6.1 三个核心源码文件和许可证已落盘，主机侧 `gcc -std=c99 -Wall -Wextra -pedantic` 编译通过，Keil `.uvprojx` XML 解析通过；尚未完成 Keil 实际构建和目标板 UART/RTT 输出验证。

## 7. 后续步骤

1. 恢复 GitHub 下载能力，获取并记录 Unity 官方版本与 commit。
2. 用官方三件套替换当前核心兼容层，保留本工程 `unity_config.h` 的输出适配。
3. 建立独立 Keil 测试 target，接入 AHT21 驱动的边界、CRC 和异常返回测试。
4. 用 UART 或 SEGGER RTT 输出测试结果，并把运行结果追加到本节。

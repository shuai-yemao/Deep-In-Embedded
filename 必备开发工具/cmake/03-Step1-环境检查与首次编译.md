---
date: 2026-07-21
tags:
  - CMake
  - STM32
  - 学习笔记
  - Step1
---

# Step 1: 环境检查与首次编译

## 目标

确认 ARM GCC 工具链、Ninja、CMake 可用，尝试首次编译。

## 操作记录

### 1. 检查工具链

```bash
arm-none-eabi-gcc --version    # ✅ GNU Arm Embedded Toolchain 10.3-2021.10
ninja --version                # 初报错 → 发现 MSYS2 自带 ✅ 1.13.2
cmake --version                # ✅ 4.3.2
```

### 2. 遇到的问题

**问题 1：PowerShell 找不到 ninja**

- PowerShell 默认 PATH 不包含 `C:\msys64\mingw64\bin`
- 解决：切换到 Git Bash 终端（MSYS2 路径自动加入 PATH）

**问题 2：GateGuard 反复拦截文件写入操作**

- 需要先陈述"事实清单"再重试

### 3. CMake 配置

```bash
cmake --preset Debug
```

输出：
```
Build type: Debug
-- Configuring done (0.1s)
-- Generating done (0.1s)
-- Build files have been written to: build/Debug
```

✅ 配置成功

### 4. 首次编译（失败 → 预期之中）

```bash
cmake --build build/Debug
```

**3 个文件编译失败**，原因全是**头文件找不到**：

| 文件 | 缺失头文件 | 所属模块 |
|------|-----------|---------|
| `Core/Src/usart.c:24` | `segger_rtt.h` | DebugComponent (SEGGER RTT) |
| `Core/Src/freertos.c:28` | `drv_adapter_temp_humi.h` | Bsp 驱动适配层 |
| `Core/Src/main.c:27` | `user_init.h` | User_Task 用户任务 |

**根因**：CubeMX 只生成了 HAL + FreeRTOS 的 CMake 配置，未包含 BSP、elog、RTT、User_Task 的头文件路径。

## 学到的知识

- `cmake --preset Debug` = 读取 CMakePresets.json → 生成构建文件（= 备菜）
- `cmake --build build/Debug` = 执行编译（= 炒菜）
- 编译错误 `fatal error: xxx.h: No such file or directory` 说明 CMake 的 `target_include_directories()` 没配这个路径
- CMake 配置成功 ≠ 编译成功。配置只检查 CMakeLists.txt 语法，不检查代码依赖

---
date: 2026-07-21
tags:
  - CMake
  - STM32
  - 嵌入式
  - 学习笔记
  - 项目设计
---

# CMake 嵌入式工程改造设计方案

## 项目概况

- **芯片**: STM32F411CEU6 (Cortex-M4, 512KB Flash, 128KB RAM)
- **工程路径**: `30_stm32f411ceu6_bsp_flash_platform`
- **当前状态**: CubeMX 已生成基础 CMake（HAL + FreeRTOS + Core），BSP/elog/User_Task 未纳入
- **目标**: 通过 CMake + J-Link 编译烧录调试，无警告无错误，elog 日志正常输出

## 整体架构（库依赖树）

```
${CMAKE_PROJECT_NAME} (可执行文件 .elf)
├── stm32cubemx (INTERFACE 库 — 头文件路径 + 全局宏)
├── STM32_Drivers (OBJECT 库 — HAL 驱动)
├── FreeRTOS (OBJECT 库 — RTOS 内核)
├── elog (OBJECT 库 — EasyLogger 日志库)         ← 新增
├── bsp_platform (OBJECT 库 — BSP 层)             ← 新增
└── user_task (OBJECT 库 — 用户任务)              ← 新增
```

## 学习路线（路线 A：渐进式完善）

| 步骤 | 做什么 | 学到的 CMake 技能 |
|------|--------|-------------------|
| Step 1 | 环境检查，确认 arm-none-eabi-gcc + ninja 可用；运行现有 CMake 配置 | cmake --preset、cmake --build、理解 toolchain |
| Step 2 | 把 DebugComponent/easylogger/ 加入 CMake | add_library（OBJECT 库）、target_include_directories、target_link_libraries |
| Step 3 | 把 Bsp/（AHT21、W25Qxx、Platform）加入 CMake | add_subdirectory、多级 CMakeLists.txt |
| Step 4 | 把 User_Task/ 用户代码加入 CMake | file(GLOB ...)、组织大型项目 |
| Step 5 | 生成 hex/bin、配置 J-Link 烧录和调试 | add_custom_command、CMAKE_OBJCOPY |
| Step 6 | 编译运行，确认 elog 日志通过 RTT 正常输出 | 整合验证 |

## 修改文件清单

| 文件 | 修改内容 |
|------|---------|
| `CMakeLists.txt`（根目录） | 添加 elog/BSP/User_Task 的 add_subdirectory、链接库 |
| `cmake/stm32cubemx/CMakeLists.txt` | 不改动（CubeMX 部分保持不动） |
| `DebugComponent/easylogger/CMakeLists.txt` | 新建 — elog OBJECT 库定义 |
| `Bsp/CMakeLists.txt` | 新建 — BSP 层 OBJECT 库定义 |
| `User_Task/CMakeLists.txt` | 新建 — 用户任务 OBJECT 库定义 |

## 验收标准

1. `cmake --preset Debug` 配置成功，无错误
2. `cmake --build build/Debug` 编译成功，**0 警告 0 错误**
3. 生成 `.elf`、`.hex`、`.bin` 产物
4. J-Link 可烧录并调试
5. elog 日志通过 SEGGER RTT 正常输出

---
date: 2026-07-21
tags:
  - CMake
  - STM32
  - 嵌入式
  - 学习笔记
---

# Step 2-6: 完整 CMake 工程改造

## 改造总览

从 CubeMX 基础 CMake 出发，逐步添加 4 个模块，最终实现 0 错误 0 警告编译 + J-Link 烧录。

## 编译验证清单

| 验证项 | 结果 |
|--------|------|
| 编译器 | GCC 14.3.1 (STM32CubeCLT) |
| Generator | Ninja 1.13.2 |
| 编译错误 | 0 |
| 编译警告 | 0 |
| ELF | `build/Debug/stm32f411ceu6_bsp_platform.elf` (1.3MB) |
| HEX | `build/Debug/stm32f411ceu6_bsp_platform.hex` (142KB) |
| BIN | `build/Debug/stm32f411ceu6_bsp_platform.bin` (51KB) |
| RAM 占用 | 26,968 B / 128 KB (20.57%) |
| Flash 占用 | 51,504 B / 512 KB (9.82%) |
| J-Link 烧录 | 150 KB/s ✅ |

## 最终依赖关系图

```
stm32f411ceu6_bsp_platform.elf
├── stm32cubemx (INTERFACE) — HAL + FreeRTOS 头文件 + 全局宏
├── STM32_Drivers (OBJECT) — HAL 驱动
├── FreeRTOS (OBJECT) — RTOS 内核
├── segger_rtt (OBJECT) — SEGGER RTT 底层驱动
├── debug_obj (OBJECT) — debug.h/c 调试输出封装
│   └── elog (OBJECT, PUBLIC) — EasyLogger 日志库
│       └── segger_rtt (OBJECT, PUBLIC) — RTT 底层
├── elog (OBJECT) — elog 核心
│   ├── segger_rtt (PUBLIC) — 输出后端
│   └── stm32cubemx (PUBLIC) — FreeRTOS.h
├── bsp_platform (OBJECT) — BSP 板级支持包
│   ├── stm32cubemx (PUBLIC) — FreeRTOS.h
│   ├── debug_obj (PUBLIC) — debug.h
│   └── user_task (PUBLIC) — user_init.h
└── user_task (OBJECT) — 用户任务
    ├── stm32cubemx (PUBLIC)
    └── debug_obj (PUBLIC)
```

## 遇到的错误及修复

### 编译错误

| 错误 | 根因 | 修复方式 |
|------|------|---------|
| `segger_rtt.h` 找不到 | DebugComponent 未加入 CMake | 创建 `DebugComponent/CMakeLists.txt` |
| `FreeRTOS.h` 找不到 (elog) | elog 未声明依赖 stm32cubemx | `target_link_libraries(elog PUBLIC stm32cubemx)` |
| `debug.h` 找不到 (BSP) | bsp_platform 未声明依赖 debug_obj | `target_link_libraries(bsp_platform PUBLIC debug_obj)` |
| `elog.h` 找不到 (debug_obj) | debug_obj 未声明依赖 elog | `target_link_libraries(debug_obj PUBLIC elog)` |
| `user_init.h` 找不到 (BSP) | bsp_platform 未声明依赖 user_task | `target_link_libraries(bsp_platform PUBLIC user_task)` |

### 链接错误

| 错误 | 根因 | 修复方式 |
|------|------|---------|
| `SEGGER_RTT_Write` undefined | segger_rtt OBJECT 库未链接到 elf | 根 CMakeLists.txt 添加 `segger_rtt` |
| `debug_init` undefined | debug_obj OBJECT 库未链接到 elf | 根 CMakeLists.txt 添加 `debug_obj` |

### 工具链错误

| 错误 | 根因 | 修复方式 |
|------|------|---------|
| `.ARM.extab (READONLY)` 链接失败 | GCC 10.3.1 不支持 READONLY 关键字 | 切换到 GCC 14.3.1 (STM32CubeCLT) |
| 编译器路径不识别 | `F:/` 驱动号在 CMake 中格式问题 | 使用完整 Windows 路径 + .exe 后缀 |

### CMake 语法错误

| 错误 | 根因 |
|------|------|
| `Expected a command name, got unquoted argument` | `add_subdirectory(Bsp);` 多了分号 `;` |

## CMake 核心知识总结

### 1. target 三种类型

| 类型 | 关键字 | 说明 |
|------|--------|------|
| 可执行文件 | `add_executable` | 最终产物 (.elf) |
| OBJECT 库 | `add_library(x OBJECT)` | 编译成 .o 但不链接，最后统一链接 |
| INTERFACE 库 | `add_library(x INTERFACE)` | 只传递头文件和宏，不编译代码 |

### 2. PUBLIC vs PRIVATE

```
PUBLIC  = "我需要，链接我的也需要"
PRIVATE = "只需要我自己"
```

例：`target_link_libraries(elog PUBLIC segger_rtt)`
→ elog 需要 RTT 头文件，链接 elog 的目标也自动获得 RTT 头文件路径

### 3. 常用命令

| 命令 | 作用 | 类比 Keil |
|------|------|----------|
| `cmake --preset Debug` | 配置（生成构建文件） | 打开 .uvprojx |
| `cmake --build --preset Debug` | 编译 | 点 Build (F7) |
| `cmake --build --preset Debug 2>&1 \| grep error:` | 只看错误 | Build Output 窗口 |

### 4. 添加 hex/bin 生成

```cmake
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.bin
)
```

### 5. J-Link 烧录

```bash
# 一键烧录
JLink.exe -device STM32F411CE -if SWD -speed 4000 -autoconnect 1 -CommanderScript flash.jlink
```

flash.jlink 内容：
```
device STM32F411CE
si SWD
speed 4000
loadfile build/Debug/stm32f411ceu6_bsp_platform.hex
r
g
exit
```

---
tags: [cmake, keil, embedded, stm32, build-system, dev-tools]
aliases: [CMake开发指南, Keil转CMake]
created: 2026-07-10
---

# CMake 嵌入式开发指南

## 概述

CMake 是一个跨平台的构建系统生成器，在嵌入式开发中逐渐取代 Keil/IAR 等商业 IDE 的专有工程格式。配合 VS Code + ARM GCC，实现 **免费、标准、可版本控制** 的嵌入式开发流程。

---

## Keil vs CMake 概念对照

| Keil µVision                    | CMake + VS Code                | 说明              |
| ------------------------------- | ------------------------------ | --------------- |
| `.uvprojx` 工程文件                 | `CMakeLists.txt`               | 纯文本，可 Git 管理    |
| ARMCC/ARMCLANG 编译器              | `arm-none-eabi-gcc`            | GCC 免费开源        |
| 图形化选芯片型号                        | 工具链文件 + 编译选项                   | 需手写或 CubeMX 生成  |
| 自动链接脚本                          | `.ld` 链接脚本手写                   | STM32CubeMX 可生成 |
| F7 编译                           | `cmake --build`                | 命令行操作           |
| F8 下载                           | `cmake --build --target flash` | 需配置烧录脚本         |
| Ctrl+F5 调试                      | VS Code F5 (Cortex-Debug)      | 插件化调试           |
| RTE 图形化管理库                      | `target_link_libraries`        | 源码级包含           |
| Options → C/C++ → Define        | `target_compile_definitions()` | 宏定义配置           |
| Options → C/C++ → Include Paths | `target_include_directories()` | 头文件路径           |

---

## 环境构成

一个完整的 CMake 嵌入式项目需要以下组件：

```
project/
├── CMakeLists.txt                 # 主构建文件（核心）
├── cmake/
│   ├── gcc-arm-none-eabi.cmake    # 工具链文件（架构/编译器）
│   └── stm32cubemx/
│       └── CMakeLists.txt         # HAL 库构建配置
├── Core/
│   ├── Inc/                       # 头文件
│   └── Src/                       # 源文件
├── Drivers/                       # STM32 HAL/LL 驱动库
├── STM32F411XX_FLASH.ld           # 链接脚本（内存布局）
├── startup_stm32f411xe.s          # 启动文件
└── STM32F411CEU6_AHT21.ioc        # CubeMX 配置文件
```

### 关键文件详解

#### CMakeLists.txt（主文件）

```cmake
cmake_minimum_required(VERSION 3.22)
project(STM32F411CEU6_AHT21)

enable_language(C ASM)

# 添加源文件
add_executable(${CMAKE_PROJECT_NAME})
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Src/main.c
    Core/Src/gpio.c
    # ...
)

# 添加头文件路径
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Inc
    Drivers/STM32F4xx_HAL_Driver/Inc
)

# 添加宏定义
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    USE_HAL_DRIVER
    STM32F411xE
)
```

#### 工具链文件（cmake/gcc-arm-none-eabi.cmake）

```cmake
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TOOLCHAIN_PREFIX arm-none-eabi-)
set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)

# MCU 特定标志
set(TARGET_FLAGS "-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${TARGET_FLAGS} -Wall -fdata-sections -ffunction-sections")
set(CMAKE_C_FLAGS_DEBUG "-O0 -g3")
set(CMAKE_C_FLAGS_RELEASE "-Os -g0")
```

#### 链接脚本（.ld 文件）

```ld
MEMORY
{
    RAM    (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
    FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
}
```

---

## 常用命令速查

```bash
# 配置工程（首次或改了 CMakeLists.txt 后执行）
cmake --preset Debug

# 编译
cmake --build --preset Debug

# 编译 + 烧录（JLink）
cmake --build --preset Debug --target flash

# 清理
cmake --build --preset Debug --target clean

# Release 模式
cmake --preset Release
cmake --build --preset Release
```

---

## Keil 经验迁移要点

### ✅ 可以直接沿用的经验

- **STM32 HAL 库代码完全一样**：`HAL_GPIO_TogglePin()`、`HAL_UART_Transmit()` 等 API 不变
- **CubeMX 配置流程不变**：`.ioc` 文件通用，可同时生成 Keil 和 CMake 工程
- **调试思路一样**：设置断点、查看变量、单步执行
- **寄存器操作完全一致**：直接操作寄存器地址的方式不变

### ⚠️ 需要适应的变化

| 项目 | Keil 习惯 | CMake 做法 |
|---|---|---|
| 添加新 .c 文件 | 右键工程 → Add Files | 编辑 `CMakeLists.txt` 加一行 |
| 添加头文件路径 | 图形界面勾选 | `target_include_directories()` |
| 优化等级 | Options 里下拉选 | `-O0` / `-Os` 在工具链文件 |
| 查看编译错误 | 双击错误跳转 | 终端看输出，VS Code 也支持双击 |
| 烧录算法 | 自动匹配 | 需配置 JLink/OpenOCD 脚本 |
| 变量查看 | 实时刷新 | VS Code 调试视图支持 |
| 工程分享 | 发 .uvprojx 文件 | Git 提交 CMakeLists.txt |

### 🚨 常见踩坑

1. **路径中的空格**：JLink 和部分工具不支持路径带空格，需用临时目录中转
2. **GCC 与 ARMCC 语法差异**：内联汇编用 `__asm__` 而非 `__asm`，中断属性用 `__attribute__((interrupt))`
3. **启动文件不同**：Keil 用 `.s` 文件（ARM ASM 语法），GCC 用 GNU AS 语法（`.syntax unified`）
4. **链接脚本不同**：Keil 用 `.sct` 分散加载文件，GCC 用 `.ld` 文件
5. **调试器选择**：Keil 只支持 ULINK/JLink，CMake 可配 OpenOCD/JLink/ST-Link 任意一种

---

## 推荐的开发流程

```
编辑代码 (VS Code)
    ↓
编译 (cmake --build)
    ↓
烧录 (cmake --build --target flash)
    ↓
调试 (F5 → Cortex-Debug)
    ↓
Git 提交 (git add/commit/push)
```

或 VS Code 内一键：

```
Ctrl+Shift+B → flash → 等几秒 → 看到效果
```

---

## 相关链接

- [[Keil 开发笔记]] — 你已有的 Keil 经验可直接迁移
- [[STM32CubeMX 使用指南]] — CubeMX 生成 CMake 工程的入口
- [[VS Code 嵌入式开发配置]] — 编辑器配置
- [[JLink 调试器使用]] — 烧录与调试
- [[多芯片CMake适配指南]] — STM32/AT32/GD32/ESP32/DSP 跨芯片开发

---

#开发工具 #cmake #嵌入式 #stm32

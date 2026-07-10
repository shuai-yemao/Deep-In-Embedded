---
tags: [cmake, project-structure, file-role, embedded, stm32]
aliases: [CMake文件结构, 工程文件说明]
created: 2026-07-10
---

# CMake 嵌入式项目文件结构详解

> 以你的 `STM32F411CEU6_AHT21` 工程为蓝本，逐文件拆解。

---

## 全景图

```
STM32F411CEU6_AHT21 - 副本/
│
├── CMakeLists.txt              ← 构建系统"大脑"  ★ 核心
├── CMakePresets.json           ← 构建预设（省掉命令行参数）
│
├── cmake/
│   ├── gcc-arm-none-eabi.cmake ← 工具链文件（ARM GCC）
│   ├── starm-clang.cmake       ← ARM Clang 工具链（备用）
│   └── stm32cubemx/
│       └── CMakeLists.txt      ← HAL 库的子构建文件
│
├── STM32F411CEU6_AHT21.ioc     ← CubeMX 工程文件
├── .mxproject                  ← CubeMX 缓存（自动生成）
│
├── startup_stm32f411xe.s       ← 芯片启动文件
├── STM32F411XX_FLASH.ld        ← 链接脚本（内存布局）
│
├── Core/                       ← 你的应用代码
│   ├── Inc/                    ← 头文件 (.h)
│   └── Src/                    ← 源文件 (.c)
│
├── Drivers/                    ← ST 官方驱动库（不需要改）
│   ├── STM32F4xx_HAL_Driver/  ← HAL 驱动源码
│   └── CMSIS/                  ← ARM 核心层
│
├── build/                      ← 编译产物（自动生成，不提交 Git）
│
└── .vscode/                    ← VS Code 配置
    ├── settings.json           ← 编辑器设置
    ├── tasks.json              ← 构建任务
    └── launch.json             ← 调试配置
```

---

## 逐文件详解

### 🧠 CMakeLists.txt — 构建系统 " 大脑 "

> 相当于 `.uvprojx` 的纯文本版，告诉 CMake：用什么编译器、编译哪些文件、链接哪些库。

```cmake
cmake_minimum_required(VERSION 3.22)
project(STM32F411CEU6_AHT21)

enable_language(C ASM)

# 添加用户代码
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Src/main.c
    Core/Src/gpio.c
    Core/Src/usart.c
    # ...
)

# 添加头文件路径
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Inc
    Drivers/STM32F4xx_HAL_Driver/Inc
)

# 添加宏定义（相当于 Keil 的 Define: USE_HAL_DRIVER,STM32F411xE）
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    USE_HAL_DRIVER STM32F411xE
)
```

| 关键函数 | 作用 | Keil 对应 |
|---|---|---|
| `project()` | 定义工程名 | 工程名 |
| `add_executable()` | 声明生成 .elf |  Output → Name of Executable |
| `target_sources()` | 添加 .c 文件 | Add Files to Group |
| `target_include_directories()` | 添加头文件路径 | Options → C/C++ → Include Paths |
| `target_compile_definitions()` | 添加宏定义 | Options → C/C++ → Define |
| `target_link_libraries()` | 链接库 | Options → Linker → Misc |

---

### ⚙️ CMakePresets.json — 构建预设

> 把 `cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=...` 这一长串参数存成 " 预设 "，以后只需 `cmake --preset Debug`。

```json
{
    "version": 3,
    "configurePresets": [
        {
            "name": "Debug",
            "generator": "Ninja",
            "binaryDir": "${sourceDir}/build/${presetName}",
            "toolchainFile": "${sourceDir}/cmake/gcc-arm-none-eabi.cmake"
        }
    ]
}
```

| 字段 | 含义 | 不预设你得手动敲什么 |
|---|---|---|
| `generator` | 构建后端 | `-G Ninja` |
| `binaryDir` | 输出目录 | `-B build/Debug` |
| `toolchainFile` | 工具链 | `-DCMAKE_TOOLCHAIN_FILE=...` |

> **本质**：一个 " 快捷方式 "，省得每次配置敲一长串。

---

### 🔧 cmake/gcc-arm-none-eabi.cmake — 工具链文件

> 告诉 CMake：这是交叉编译，用 `arm-none-eabi-gcc` 而不是电脑的本地编译器。

```cmake
set(CMAKE_SYSTEM_NAME Generic)       # 不是 Windows/Linux，是裸机
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TOOLCHAIN_PREFIX arm-none-eabi-)
set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)

# MCU 专属编译选项
set(TARGET_FLAGS "-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard")
set(CMAKE_C_FLAGS_DEBUG "-O0 -g3")        # Debug: 不优化
set(CMAKE_C_FLAGS_RELEASE "-Os -g0")      # Release: 优化尺寸
```

**换芯片时改什么**：改 `TARGET_FLAGS` 里的 `-mcpu` 和 `-mfpu`。

| 芯片 | cortex-m? | FPU |
|---|---|---|
| STM32F103 (M3) | `cortex-m3` | 无 |
| STM32F411 (M4F) | `cortex-m4` | `fpv4-sp-d16` |
| STM32H743 (M7) | `cortex-m7` | `fpv5-d16` |
| AT32F403A (M4F) | `cortex-m4` | `fpv4-sp-d16` |

---

### 📦 cmake/stm32cubemx/CMakeLists.txt — HAL 库子构建

> 管理整个 HAL 驱动库的编译，CubeMX 自动生成，一般不需要手动改。

```cmake
# 列出所有需要编译的 HAL 源文件
set(STM32_Drivers_Src
    Core/Src/system_stm32f4xx.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_uart.c
    # ...
)

# 打包成一个静态库
add_library(STM32_Drivers OBJECT)
target_sources(STM32_Drivers PRIVATE ${STM32_Drivers_Src})
```

**什么时候要改**：当你用 CubeMX 添加了新外设（SPI、I2C、ADC），需要把新生成的 .c 文件也加进来。

---

### 🚀 startup_stm32f411xe.s — 启动文件

> 芯片上电后执行的第一段代码，用汇编写的。

**它的工作**：

1. 定义中断向量表（告诉 CPU 中断来了跳哪里）
2. 设置栈指针 SP
3. 清零 `.bss` 段（未初始化全局变量）
4. 复制 `.data` 段（从 Flash 复制到 RAM）
5. 调用 `SystemInit()` → `main()`

**常见启动文件命名**：

- `startup_stm32f411xe.s` — STM32F411xE
- `startup_stm32f407xx.s` — STM32F407
- `startup_stm32f103xb.s` — STM32F103

> **换芯片必换启动文件**。不同芯片的 Flash/SRAM 大小不同，中断向量数也不同。

---

### 🔗 STM32F411XX_FLASH.ld — 链接脚本

> 告诉链接器：Flash 和 RAM 各多大、代码放哪、变量放哪。

```ld
MEMORY
{
    RAM    (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
    FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
}
```

| 芯片 | Flash 地址 | Flash 大小 | RAM 地址 | RAM 大小 |
|---|---|---|---|---|
| STM32F411CE | `0x08000000` | 512KB | `0x20000000` | 128KB |
| STM32F103C8 | `0x08000000` | 64KB | `0x20000000` | 20KB |
| STM32F407VG | `0x08000000` | 1024KB | `0x20000000` | 192KB |
| AT32F403AC | `0x08000000` | 512KB | `0x20000000` | 96KB |

> **换芯片必改链接脚本**。Flash/RAM 大小对不上，程序跑不起来。

---

### 🧩 STM32F411CEU6_AHT21.ioc — CubeMX 工程文件

> STM32CubeMX 的图形化配置文件。双击它就打开 CubeMX 图形界面。

**.ioc 文件管理的内容**：

- 芯片型号
- 引脚功能分配（PA0→USART1_TX, PB13→LED 等）
- 时钟树配置
- 外设参数（UART 波特率、GPIO 速度等）
- 中间件配置（FreeRTOS、USB、FATFS 等）

**工作流**：

```
改 .ioc → CubeMX 重新生成代码 → 重新编译
```

---

### 📋 Core/Inc/ 和 Core/Src/ — 你的应用代码

| 文件                     | 作用                      | 你改不改                 |
| ---------------------- | ----------------------- | -------------------- |
| `main.c`               | 主程序入口                   | ✅ 频繁改（你的业务逻辑）        |
| `main.h`               | 全局宏定义（如引脚号）             | ✅ 偶尔改                |
| `gpio.c` / `gpio.h`    | GPIO 初始化                | ⚠️ CubeMX 生成，也可手改    |
| `usart.c` / `usart.h`  | USART 初始化               | ⚠️ CubeMX 生成         |
| `stm32f4xx_it.c`       | 中断服务函数                  | ✅ 加中断处理              |
| `stm32f4xx_hal_msp.c`  | HAL 底层接口初始化             | ⚠️ CubeMX 生成         |
| `stm32f4xx_hal_conf.h` | HAL 功能开关                | ⚠️ 启用/禁用外设模块         |
| `syscalls.c`           | `printf` 底层实现（`_write`） | 🔧 如果你想用 printf 串口打印 |
| `sysmem.c`             | `malloc` 堆管理            | 🔧 改堆大小              |
| `system_stm32f4xx.c`   | 系统时钟配置（168MHz）          | ⚠️ CubeMX 生成         |

**快速判断**：

- CubeMX 生成的文件一般有 `/* USER CODE BEGIN */` 和 `/* USER CODE END */` 标记
- 在这两个标记**之间**写的代码，重新生成不会被覆盖
- 之外的代码会被 CubeMX 覆盖

---

### 📚 Drivers/ — ST 官方驱动库（不需要改）

```
Drivers/
├── STM32F4xx_HAL_Driver/     ← HAL 库源码（stm32f4xx_hal_*.c/.h）
│   ├── Inc/                   ← 头文件
│   └── Src/                   ← 源文件
│
└── CMSIS/                     ← ARM 核心层
    ├── Include/               ← core_cm4.h, cmsis_gcc.h 等
    └── Device/ST/STM32F4xx/  ← stm32f411xe.h（芯片寄存器定义）
```

| 目录                           | 包含什么                                               |
| ---------------------------- | -------------------------------------------------- |
| `CMSIS/Include/`             | `core_cm4.h`（M4 内核寄存器操作）、`cmsis_gcc.h`（GCC 内联函数）   |
| `CMSIS/Device/ST/STM32F4xx/` | `stm32f411xe.h`（这个芯片所有的外设寄存器地址）                    |
| `HAL_Driver/Inc/`            | `stm32f4xx_hal.h`、`stm32f4xx_hal_gpio.h` 等 API 头文件 |
| `HAL_Driver/Src/`            | `stm32f4xx_hal.c`、`stm32f4xx_hal_gpio.c` 等实现代码     |
|                              |                                                    |

> **不需要修改**，除非你发现 Bug 要打补丁。

---

### 📁 build/ — 编译产物（自动生成，不要手动改）

```
build/Debug/
├── CMakeCache.txt          ← CMake 配置缓存
├── build.ninja             ← Ninja 构建脚本（CMake 自动生成的 Makefile 等价物）
├── compile_commands.json   ← clangd 代码补全用的索引
│
├── *.o                     ← 编译出来的目标文件（.c → .o）
├── *.elf                   ← 最终的 ELF 可执行文件
├── *.bin                   ← 纯二进制（烧录用）
├── *.hex                   ← Intel HEX 格式（烧录用）
├── *.map                   ← 链接映射表（看变量/函数地址、内存占用）
│
└── flash.jlink             ← JLink 烧录脚本（自动生成）
```

| 文件 | 作用 | 什么时候看 |
|---|---|---|
| `*.elf` | 带调试信息的可执行文件 | 调试时 |
| `*.bin` | 纯二进制，烧录到 Flash | 烧录时 |
| `*.hex` | Intel HEX，含地址信息 | 烧录时 |
| `*.map` | 每个变量/函数的地址和大小 | 🔍 **排查 Flash/RAM 超限** |
| `*.o` | 每个 .c 文件编译后的目标文件 | 一般不关心 |

> **build/ 不提交 Git**，在 `.gitignore` 里加 `/build/`。

---

## 初学者最容易混淆的点

### ❓ 哪些是 CubeMX 生成的，哪些是自己写的？

```
CubeMX 生成（不改文件名）：
  Core/Inc/*.h
  Core/Src/main.c, gpio.c, usart.c, stm32f4xx_it.c, ...
  Drivers/STM32F4xx_HAL_Driver/
  Drivers/CMSIS/
  startup_stm32f411xe.s
  STM32F411XX_FLASH.ld

手写（或从模板复制）：
  CMakeLists.txt
  CMakePresets.json
  cmake/gcc-arm-none-eabi.cmake

你业务代码写在哪里：
  Core/Src/main.c 的 USER CODE 区域
```

### ❓ .ld 和 .s 的区别

```
.s 启动文件：上电先跑谁（中断向量表、初始化 C 环境）
.ld 链接脚本：代码放哪里（Flash/RAM 分配）

.s 不对 → 连 main() 都进不去
.ld 不对 → 编译能过，跑起来随机死机
```

### ❓ 为什么有多个 CMakeLists.txt？

```
根目录 CMakeLists.txt         → 主工程（你的应用）
cmake/stm32cubemx/CMakeLists.txt → 子工程（HAL 驱动库）

结构类似于：
  主程序 (main.c)  →  链接 →  HAL 库 (.o files)
```

---

## 快速判断：这是什么文件？

| 看到文件名              | 判断        | 怎么办                    |
| ------------------ | --------- | ---------------------- |
| `*.c` / `*.h`      | 代码        | 该写的写                   |
| `CMakeLists.txt`   | 构建配置      | 改了要重新 `cmake --preset` |
| `*.cmake`          | 工具链/辅助    | 换芯片才改                  |
| `*.ld`             | 链接脚本      | 换芯片必改                  |
| `*.s`              | 启动文件      | 换芯片必换                  |
| `*.ioc`            | CubeMX 配置 | 图形界面打开                 |
| `*.json` (presets) | 构建预设      | 一般不动                   |
| `build/`           | 编译产物      | 不看、不提交 Git             |

---

## 相关笔记

- [[CMake嵌入式开发指南]] — Keil 到 CMake 入门
- [[多芯片CMake适配指南]] — 各芯片适配
- [[CMake与IDE优缺点对比]] — 为什么用 CMake
- [[STM32CubeMX 使用指南]] — .ioc 使用方法

---

#开发工具 #cmake #项目结构 #嵌入式 #stm32

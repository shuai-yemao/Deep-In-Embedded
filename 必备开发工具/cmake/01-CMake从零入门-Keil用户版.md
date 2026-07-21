---
date: 2026-07-21
tags:
  - CMake
  - STM32
  - 嵌入式
  - 学习笔记
---

# CMake 从零入门（Keil 用户版）

## 一、CMake 到底是什么？

**一句话：CMake 是一个"生成编译配置的工具"，不是编译器，也不是 IDE。**

你在 Keil 里做这些事情：

- 右键点"Add Existing Files to Group"添加源文件
- 点"Options for Target"设置头文件路径
- 写 `#define USE_HAL_DRIVER` 到预定义宏
- 点 "Build (F7)" 开始编译

**CMake 就是把这些手动点击操作，换成一文本文件（CMakeLists.txt）来描述**，然后自动给你生成一套 Makefile 或 Ninja 等"编译脚本"，最后执行编译。

### Keil vs CMake 对照表

| Keil 操作 | CMake 写法 |
|---|---|
| 右键 → Add Existing Files to Group | `target_sources(my_target PRIVATE src/main.c)` |
| Options → C/C++ → Include Paths | `target_include_directories(my_target PRIVATE Core/Inc)` |
| Options → C/C++ → Define | `target_compile_definitions(my_target PRIVATE USE_HAL_DRIVER STM32F411xE)` |
| Options → Linker → Scatter File | `target_link_options(my_target PRIVATE -T stm32f411ceux.ld)` |
| 点 Build (F7) | `cmake --build .` |

**核心思路：把图形界面里"点一下"的每个操作，换成 CMakeLists.txt 里的一行文本。**

---

## 二、CMake 的工作流程：用做饭打比方

CMake 构建分 **两个阶段**，不是一步到位的。

### 阶段 1：配置（备菜）

```bash
cmake -B build -G Ninja
```

- `-B build`：指定"在 build 文件夹里操作"
- `-G Ninja`：选择用 Ninja 这个"灶台"（也可以 `-G "Unix Makefiles"`）

**这个阶段做了什么事？**

- 读你的 CMakeLists.txt（食谱）
- 检查编译器（arm-none-eabi-gcc）有没有装好
- 检查头文件、源文件路径对不对
- 把所有"准备知识"整理好，生成一份"清单"（Makefile 或 build.ninja）

**类比**：你站在厨房里，先把菜谱读一遍 —— 确认有锅、有灶、冰箱里有菜、调味料齐全。**还没开始炒菜。**

### 阶段 2：构建（炒菜）

```bash
cmake --build build
```

**这个阶段做了什么事？**

- 执行阶段 1 生成的"清单"里的每一条命令
- 挨个编译每个 .c 文件 → .o 文件
- 链接所有 .o 文件 → .elf / .hex / .bin

**类比**：开火、倒油、下菜、翻炒。**这才真正开始干活。**

### 产出：上桌

编译成功后你会得到：

- `something.elf`（含调试信息，可以给 J-Link/GDB 用）
- `something.hex`（Intel Hex 格式，给烧录器用）
- `something.bin`（纯二进制，给量产烧录用）

**类比**：菜炒好了，装盘上桌。三种格式就像"热菜、冷盘、打包盒"，内容一样，包装不同。

---

## 三、核心概念：用大白话拆解

### 3.1 `CMakeLists.txt` —— 食谱

每个项目的根目录下都会有一个 `CMakeLists.txt`。

**这是 CMake 唯一认识的文件。** 你所有的配置、源文件列表、头文件路径、链接选项，全部写在这个文件里。

```
my_project/
├── CMakeLists.txt       ← 食谱
├── Core/
│   ├── Inc/
│   │   └── main.h
│   └── Src/
│       └── main.c
└── build/               ← 备菜、炒菜都在这个文件夹里
```

**类比 Keil**：`CMakeLists.txt` 相当于 Keil 的 `.uvprojx` 文件 —— 记录了所有项目信息。只不过 Keil 的 .uvprojx 是 GUI 帮你写的，CMakeLists.txt 是你自己手写的。

### 3.2 `target` —— 你要做的一道菜

一个 target 就是你最终要生成的东西。

- `add_executable(firmware.elf main.c)` —— 生成一个可执行文件（你的固件）
- `add_library(my_lib STATIC utils.c)` —— 生成一个静态库（比如你写的一个传感器驱动库）

**一条项目里可以有多个 target。** 比如：

```
传感器驱动 → libsensors.a（库）
业务逻辑    → firmware.elf（可执行文件，链接 libsensors.a）
```

**类比 Keil**：一个 Keil 项目里可以包含多个 Target（在"Manage Project Items"里切换），每个 Target 有不同的源文件列表和编译选项。CMake 的 target 就是同样的概念。

### 3.3 `generator` —— 你用哪种灶台

Generator 是 CMake 用来"生成构建脚本"的工具。

| Generator | 特点 | 适合场景 |
|---|---|---|
| `Ninja` | 快，轻量，默认并行编译 | 日常开发，推荐 |
| `Unix Makefiles` | 经典，兼容性好 | Linux 下通用 |
| `MinGW Makefiles` | Windows 下用 | 没有 Ninja 时 |

**选择方式**：`cmake -B build -G Ninja`

**类比 Keil**：Keil 的编译器（ArmCC / ArmClang）是绑定在 IDE 里的。CMake 不绑定编译器，而是通过 generator 生成"编译脚本"，再由 Ninja/Make 去调用编译器。

### 3.4 `toolchain` —— 你的锅铲品牌

Toolchain 文件告诉 CMake：

- 用什么编译器（arm-none-eabi-gcc？armclang？）
- 编译器在哪个路径
- 用什么链接器、汇编器

STM32 通常用 **arm-none-eabi-gcc** 工具链。

Toolchain 文件长这样：

```cmake
# arm-none-eabi-gcc.cmake
set(CMAKE_C_COMPILER   arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_AR           arm-none-eabi-ar)
```

在配置时传入：

```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=arm-none-eabi-gcc.cmake
```

**类比 Keil**：Keil 里你在 "Options → Target" 选择芯片型号（STM32F411CE），Keil 自动帮你选好编译器（ArmCC v6）。CMake 里你手动指定 toolchain 文件，告诉 CMake "我这个项目的编译器是 arm-none-eabi-gcc"。

### 3.5 `preset` —— 保存好的配方参数

每次配置都写长长的一串命令很烦：

```bash
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=arm-none-eabi-gcc.cmake -DCMAKE_BUILD_TYPE=Debug
```

**Preset（预设）** 就是把这些参数打包起来，存到 `CMakePresets.json` 文件里：

```json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "debug",
      "generator": "Ninja",
      "toolchainFile": "arm-none-eabi-gcc.cmake",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug"
      }
    }
  ]
}
```

之后只需要：

```bash
cmake --preset debug
cmake --build build/debug
```

**类比 Keil**：存一个 `.uvprojx` 文件，所有配置都在里面，下次打开直接能用。Preset 就是帮你记住"这次用 Debug 配置，那个灶台，那个锅铲"。

---

## 四、三个最重要的 CMake 命令（必背）

### 4.1 `target_include_directories` —— "去哪里找 .h 文件"

```cmake
target_include_directories(firmware.elf PRIVATE
    Core/Inc
    Drivers/STM32F4xx_HAL_Driver/Inc
)
```

**PRIVATE** 表示"只有这个 target 能看到这些路径"。

**类比 Keil**：Options for Target → C/C++ → Include Paths，一行一个路径。

---

### 4.2 `target_sources` —— "哪些 .c 文件参与编译"

```cmake
target_sources(firmware.elf PRIVATE
    Core/Src/main.c
    Core/Src/gpio.c
    Core/Src/usart.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c
)
```

**注意**：也可以把源文件写在 `add_executable` 里，但推荐用 `target_sources` 分开写，更清晰。

**类比 Keil**：右键 → Manage Project Items → 把 .c 文件拖进去。

---

### 4.3 `target_link_libraries` —— "需要链接哪些库"

```cmake
target_link_libraries(firmware.elf PRIVATE
    sensors         # 链接你自己编译的传感器库
    m               # 链接 math 库（是的，m 是 libm）
)
```

还可以链接**接口库**（interface library），用来传递编译选项而不用复制粘贴：

```cmake
# 创建一个"编译选项包"
add_library(stm32_opts INTERFACE)
target_compile_options(stm32_opts INTERFACE -mcpu=cortex-m4 -mthumb)
target_compile_definitions(stm32_opts INTERFACE STM32F411xE)

# 其他 target 直接链接这个"包"就继承了所有选项
target_link_libraries(firmware.elf PRIVATE stm32_opts)
```

**类比 Keil**：Options for Target → Linker → Misc Controls，以及你在 Group 上右键设置的编译选项。或者你项目里引用了 `ARM.LPC17xx.Lib` 这样的库文件。

---

## 五、一个完整的 STM32 CMakeLists.txt 长什么样

看一个最小示例，你立刻就能对上 Keil 的操作：

```cmake
cmake_minimum_required(VERSION 3.20)
project(stm32f411_demo ASM C CXX)

# 指定芯片架构和浮点特性
add_compile_options(-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16)

# 创建 target —— 就是你的固件
add_executable(firmware.elf)

# 告诉 CMake 去哪找 .h 文件（= Keil 的 Include Paths）
target_include_directories(firmware.elf PRIVATE
    Core/Inc
    Drivers/STM32F4xx_HAL_Driver/Inc
)

# 告诉 CMake 哪些 .c 要编译（= Keil 的源文件列表）
target_sources(firmware.elf PRIVATE
    Core/Src/main.c
    Core/Src/stm32f4xx_it.c
    Core/Src/system_stm32f4xx.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c
    startup/startup_stm32f411xe.s       # 汇编启动文件
)

# 告诉 CMake 预定义宏（= Keil 的 Define 框）
target_compile_definitions(firmware.elf PRIVATE
    USE_HAL_DRIVER
    STM32F411xE
)

# 告诉 CMake 用哪个链接脚本
target_link_options(firmware.elf PRIVATE
    -TSTM32F411CEUx_FLASH.ld
)

# 最终输出 hex 和 bin 文件
set(HEX_FILE ${CMAKE_BINARY_DIR}/firmware.hex)
set(BIN_FILE ${CMAKE_BINARY_DIR}/firmware.bin)
add_custom_command(TARGET firmware.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex firmware.elf ${HEX_FILE}
    COMMAND ${CMAKE_OBJCOPY} -O binary firmware.elf ${BIN_FILE}
)
```

---

## 六、一张图总结：你的 Keil 习惯映射到 CMake

```
Keil 里你怎么做？              CMake 里你写什么？
──────────────────────────────────────────────────
File → New Project            cmake_minimum_required() + project()
Add Source Files              target_sources()
Add Include Paths             target_include_directories()
#define 预定义宏               target_compile_definitions()
选芯片型号                    -mcpu=cortex-m4 编译选项 + 链接脚本
点 Build (F7)                cmake --build build
点 Download (F8)             （需要额外配置烧录命令或 IDE）
```

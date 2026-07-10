---
tags: [cmake, project-management, file-organization, workflow]
aliases: [CMake项目管理, 添加文件, 组织架构]
created: 2026-07-10
---

# CMake 项目文件管理指南

> 在 Keil 里"右键 → Add Files"的事，在 CMake 里怎么做。

---

## 核心原则

```
Keil 管理方式：  图形化拖拽  →  .uvprojx 自动记录
CMake 管理方式：  创建文件    →  手动在 CMakeLists.txt 注册
```

**每次新建 .c/.h 文件，都必须告诉 CMake**，否则编译时找不到。

---

## 场景一：在已有模块中添加一个函数

> 最简单的场景，不改文件结构。

**需求**：在 `main.c` 里加一个 `delay_ms()` 函数

**操作**：
```
1. 打开 Core/Src/main.c
2. 在 USER CODE 区域写函数
3. 保存 → 重新编译
```

**不需要改 CMakeLists.txt**，因为 `main.c` 已经注册过了。

```bash
cmake --build --preset Debug   # 只编译改了的文件
```

---

## 场景二：添加新的 .c/.h 文件（最常见）

> 比如你要写一个 `i2c_aht21.c` 驱动。

### 📝 第一步：创建文件

```
STM32F411CEU6_AHT21 - 副本/
├── Core/
│   ├── Inc/
│   │   ├── main.h
│   │   └── i2c_aht21.h        ← 新建
│   └── Src/
│       ├── main.c
│       └── i2c_aht21.c        ← 新建
```

```bash
# 创建空文件
touch Core/Inc/i2c_aht21.h
touch Core/Src/i2c_aht21.c
```

**文件内容示例**：

```c
// Core/Inc/i2c_aht21.h
#ifndef __I2C_AHT21_H
#define __I2C_AHT21_H

#include "main.h"

HAL_StatusTypeDef AHT21_Init(void);
HAL_StatusTypeDef AHT21_Read(float *temp, float *humid);

#endif
```

### 📝 第二步：注册到 CMakeLists.txt

打开根目录的 `CMakeLists.txt`，找到这部分：

```cmake
# 修改前
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Src/main.c
    Core/Src/gpio.c
    Core/Src/usart.c
    Core/Src/stm32f4xx_it.c
    Core/Src/stm32f4xx_hal_msp.c
    Core/Src/sysmem.c
    Core/Src/syscalls.c
)
```

改成：

```cmake
# 修改后
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Src/main.c
    Core/Src/gpio.c
    Core/Src/usart.c
    Core/Src/stm32f4xx_it.c
    Core/Src/stm32f4xx_hal_msp.c
    Core/Src/sysmem.c
    Core/Src/syscalls.c
    Core/Src/i2c_aht21.c        ← 加这一行
)
```

如果是 CubeMX 管理的子构建，你还要改 `cmake/stm32cubemx/CMakeLists.txt`：

```cmake
# cmake/stm32cubemx/CMakeLists.txt
set(MX_Application_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/gpio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/usart.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/stm32f4xx_it.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/stm32f4xx_hal_msp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sysmem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/syscalls.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/i2c_aht21.c    ← 加这一行
)
```

### 📝 第三步：重新配置 + 编译

```bash
# 改了 CMakeLists.txt 必须重新配置
cmake --preset Debug

# 再编译
cmake --build --preset Debug
```

> **为什么改 CMakeLists.txt 要重新 `cmake --preset`？**
> 因为 `cmake --build` 只看 .c 文件有没有变化，不看 CMakeLists.txt。
> 改了构建配置必须重新生成 `build.ninja`。

---

## 场景三：添加头文件路径

> 当你把 .h 文件放在非 `Core/Inc` 目录时。

比如你把驱动统一放到 `Drivers/MyDrivers/Inc/`：

```cmake
# CMakeLists.txt
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Inc
    Drivers/STM32F4xx_HAL_Driver/Inc
    Drivers/CMSIS/Device/ST/STM32F4xx/Include
    Drivers/CMSIS/Include
    Drivers/MyDrivers/Inc          ← 加这一行
)
```

然后源文件用 `#include "my_driver.h"` 就能找到。

---

## 场景四：创建新的模块子目录

> 当一个模块有多个 .c/.h 文件时，建议单独建文件夹。

```
Core/
├── Inc/
├── Src/
│   ├── main.c
│   └── ...
└── Modules/                     ← 新建
    ├── AHT21/
    │   ├── aht21.c
    │   ├── aht21.h
    │   └── CMakeLists.txt       ← 新建（子模块自己的构建）
    ├── OLED/
    │   ├── oled.c
    │   ├── oled.h
    │   └── CMakeLists.txt
    └── CMakeLists.txt           ← 新建（汇总子模块）
```

**方式一：最简单的，直接注册路径**

```cmake
# 根 CMakeLists.txt
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Src/main.c
    Core/Src/gpio.c
    # ...
    Core/Modules/AHT21/aht21.c
    Core/Modules/OLED/oled.c
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Inc
    Core/Modules/AHT21
    Core/Modules/OLED
)
```

**方式二：用子目录管理（推荐，模块多了这样清爽）**

```cmake
# 根 CMakeLists.txt — 只加一行
add_subdirectory(Core/Modules)
```

```cmake
# Core/Modules/CMakeLists.txt — 汇总子模块
add_subdirectory(AHT21)
add_subdirectory(OLED)
```

```cmake
# Core/Modules/AHT21/CMakeLists.txt — 每个模块自己的
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/aht21.c
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
```

> 方式二的好处：修改某个模块只改它的 `CMakeLists.txt`，不影响其他模块。

---

## 场景五：添加第三方库

> 比如你要用 FatFS、LVGL、FreeRTOS。

### 方法一：直接把源码放进来

```
Drivers/
├── STM32F4xx_HAL_Driver/
├── CMSIS/
└── ThirdParty/                  ← 新建
    ├── FreeRTOS/
    │   ├── Source/
    │   └── ...
    └── CMakeLists.txt
```

```cmake
# Drivers/ThirdParty/CMakeLists.txt
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    FreeRTOS/Source/tasks.c
    FreeRTOS/Source/timers.c
    FreeRTOS/Source/list.c
    FreeRTOS/Source/portable/GCC/ARM_CM4F/port.c
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    FreeRTOS/Source/include
    FreeRTOS/Source/portable/GCC/ARM_CM4F
)
```

### 方法二：Git 子模块（推荐）

```bash
git submodule add https://github.com/FreeRTOS/FreeRTOS-Kernel.git Drivers/FreeRTOS
```

然后在 CMake 里引用：

```cmake
add_subdirectory(Drivers/FreeRTOS)
```

### 方法三：FetchContent（自动下载，不需要手动 clone）

```cmake
# CMakeLists.txt
include(FetchContent)
FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.0.0
)
FetchContent_MakeAvailable(lvgl)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE lvgl)
```

---

## 哪个文件放哪？推荐目录规范

```
STM32F411CEU6_AHT21 - 副本/       ← 根目录（放 CMakeLists.txt + 配置文件）
│
├── Core/                         ← 核心代码
│   ├── Inc/                      ← 头文件
│   ├── Src/                      ← 源文件
│   └── Modules/                  ← 功能模块（按功能分文件夹）
│       ├── AHT21/
│       ├── OLED/
│       └── W25Qxx/
│
├── Drivers/                      ← 驱动库（通常不改）
│   ├── STM32F4xx_HAL_Driver/
│   ├── CMSIS/
│   └── ThirdParty/               ← 第三方库
│       ├── FreeRTOS/
│       ├── LVGL/
│       └── FatFS/
│
├── Middlewares/                   ← 中间件
│
├── Docs/                         ← 文档
│
├── Scripts/                      ← 辅助脚本（Python 等）
│
├── Test/                         ← 单元测试
│
└── cmake/                        ← CMake 辅助文件
    ├── gcc-arm-none-eabi.cmake   ← 工具链
    └── stm32cubemx/
        └── CMakeLists.txt        ← HAL 子构建
```

---

## 操作速查表

| 目的 | 操作 | 是否要改 CMakeLists.txt | 是否要重新 cmake --preset |
|---|---|---|---|
| 在已有 .c 里加函数 | 直接写代码 | ❌ | ❌ |
| 新建 .c/.h 文件 | 创建文件 + 注册 | ✅ | ✅ |
| 新建头文件路径 | 创建目录 + 注册 | ✅ | ✅ |
| 新建模块目录 | 创建目录 + 子 CMakeLists.txt | ✅ | ✅ |
| 添加第三方库 | clone / submodule / FetchContent | ✅ | ✅ |
| 删除文件 | 删文件 + 取消注册 | ✅ | ✅ |
| 重命名文件 | 改名 + 更新 CMakeLists.txt | ✅ | ✅ |
| 只改 .h 内容 | 直接改 | ❌ | ❌ |

---

## 常见错误

### ❌ 新建了文件忘了注册

```
fatal error: i2c_aht21.h: No such file or directory
  → .h 文件没在 target_include_directories 里登记
```

```
undefined reference to `AHT21_Init'
  → .c 文件没在 target_sources 里登记
```

### ❌ 改了 CMakeLists.txt 忘了重新配置

```
改了 CMakeLists.txt 但只执行 cmake --build
→ CMake 不知道文件列表变了，编译的还是老版本
```

**正确的流程**：
```bash
# 改了 CMakeLists.txt 后
cmake --preset Debug          # 重新配置
cmake --build --preset Debug  # 再编译
```

### ❌ 在 USER CODE 区域外写代码

```
CubeMX 重新生成代码 → 你写在外面代码被覆盖丢失
```

**正确做法**：始终在 `/* USER CODE BEGIN */` 和 `/* USER CODE END */` **之间**写代码。

---

## 推荐工作流

```
1. 新建文件
   touch Core/Modules/XXX/xxx.c
   touch Core/Modules/XXX/xxx.h

2. 写代码
   vim/VS Code 编辑 xxx.c 和 xxx.h

3. 注册到 CMake
   vim CMakeLists.txt
   → target_sources 加一行
   → target_include_directories 加一行

4. 编译测试
   cmake --preset Debug
   cmake --build --preset Debug

5. 提交 Git
   git add Core/Modules/XXX/ CMakeLists.txt
   git commit -m "feat: add XXX module"
```

> **等习惯了，三步走就像肌肉记忆**：新建文件 → 改 CMakeLists.txt → 编译验证。

---

## 相关笔记

- [[CMake嵌入式开发指南]] — CMake 入门
- [[CMake项目文件结构详解]] — 每个文件的作用
- [[多芯片CMake适配指南]] — 换芯片的文件改动
- [[STM32CubeMX 使用指南]] — .ioc 文件用法

---

#开发工具 #cmake #项目管理 #文件组织 #嵌入式

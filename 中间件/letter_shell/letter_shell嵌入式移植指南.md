---
title: letter_shell 嵌入式移植指南
date: 2026-08-05
tags:
  - 嵌入式
  - 中间件
  - letter_shell
  - STM32
  - FreeRTOS
  - CMake
  - J-Link
aliases:
  - letter_shell 嵌入式移植
  - Letter Shell 移植
---

# letter_shell 嵌入式移植指南

> [!summary] 这篇指南解决什么问题
> 把 letter_shell v3.2.4 嵌入式 shell 移植到 STM32F411CEU6 + FreeRTOS + GCC/CMake 工程，使用 USART1 DMA+ 空闲中断接收，统一通过自研 OSAL 抽象层调度 shell 任务，并说明源码来源、文件分层、链接脚本、CMake 集成、端口实现与验证方法。

## 0. 先看结论

本工程（`letter_shell` 分支）已经完成以下集成：

- 官方 letter_shell v3.2.4 源码已放入 `Middlewares/letter_shell/`，按 `inc/ src/ port/` 三层分层。
- 上游核心代码（`shell.c / shell.h / shell_cfg.h / shell_ext.* / shell_cmd_list.c / shell_companion.c`）未做修改，只补了一行 `#include <stddef.h>` 解决 `size_t` 缺失。
- 端口层 `port/shell_port.c` 实现：USART1 DMA+IDLE 接收 → 环形缓冲（512 B）→ OSAL 二值信号量唤醒 → OSAL 互斥锁保护输出 → OSAL 任务承载 `shellTask`。
- 链接脚本 `STM32F411XX_FLASH.ld` 新增 `.shellCommand` 段并导出 `_shell_command_start / _shell_command_end`，使 `SHELL_EXPORT_CMD` 段导出方式生效。
- CMake 增加 `letter_shell` OBJECT 库，并通过 `SHELL_CFG_USER="shell_cfg_user.h"` 编译宏让上游 `shell_cfg.h` 自动加载项目专用配置。
- FreeRTOS 默认任务 `StartDefaultTask` 中调用 `letter_shell_startup()` 后自销毁，shell 任务常驻。
- 已加入示例命令 `hello`，并在启动后内置调用 `shellRun(... "help\r")` 自检。
- GCC 全量构建通过：73/73 目标，FLASH 10.62% / RAM 23.10%。
- J-Link SWD 烧录并运行通过，串口 115200 8N1 可见 shell banner 与 `letter:/>` 提示符。

“shell 跑起来”不等于“业务命令已经齐备”。自定义命令仍需按业务逐步添加。

## 1. letter_shell 是什么

letter_shell 是 NevermindZZT 开源的轻量级嵌入式 shell，3.x 版本支持命令段导出、参数自动转换、历史命令、Tab 补全、双击 Tab 显示帮助、密码登录、按键绑定、变量导出、伴生对象等。

| 上游文件       | 作用                                                                 |
|---|---|
| `shell.c`      | shell 主逻辑：命令解析、补全、历史、登录、按键处理、任务循环             |
| `shell.h`      | Shell/ShellCommand 结构、`SHELL_EXPORT_CMD` 等导出宏、公开 API          |
| `shell_cfg.h`  | 上游默认配置（任务/历史/缓冲/锁/用户/超时等），所有宏都用 `#ifndef` 包裹  |
| `shell_ext.*`  | 参数类型自动转换扩展                                                   |
| `shell_cmd_list.c` | 内置命令实现（`help / cmds / vars / users / keys / clear / setVar` 等）|
| `shell_companion.c` | 伴生对象机制，供文件系统、日志工具等扩展使用                       |

基本运行链路：

~~~text
shellInit(&shell, buffer, size)
    -> 绑定 read / write / lock / unlock 回调
    -> 扫描 .shellCommand 段，建立命令表
shellTask(&shell)  (在 OSAL 任务中循环)
    -> shell->read(&data, 1)
    -> shellHandler(shell, data)
        -> 解析回车 / Tab / 方向键 / 普通字符
        -> 回车时 dispatch 命令
~~~

目标板上的输入/输出链路：

~~~text
PC 串口工具(115200 8N1)
    <-> PA9/PA10 USART1
        <-> DMA2_Stream5 RX + USART1 IDLE 中断
            -> 环形缓冲 s_rxRing[]
            -> OSAL 信号量 s_rxSem give
                -> shellTask take -> shell->read 弹出 1 字节
                -> shellHandler -> shell->write
                    -> HAL_UART_Transmit 阻塞发回 PC
~~~

letter_shell 不直接依赖 FreeRTOS；它只调用 `shell->read / shell->write / shell->lock / shell->unlock` 四个回调。本工程的端口层用 OSAL 包装 FreeRTOS，shell 核心不接触任何 RTOS 原语。

## 2. 官方源码地址

本次使用官方仓库 master 分支（v3.2.4），不使用本地缓存或自定义兼容实现。

- 官方仓库：[NevermindZZT/letter-shell](https://github.com/NevermindZZT/letter-shell)
- 本次拉取：`git clone --depth 1 https://github.com/NevermindZZT/letter-shell.git`
- 官方 shell.c：[src/shell.c](https://github.com/NevermindZZT/letter-shell/blob/master/src/shell.c)
- 官方 shell.h：[src/shell.h](https://github.com/NevermindZZT/letter-shell/blob/master/src/shell.h)
- 官方 shell_cfg.h：[src/shell_cfg.h](https://github.com/NevermindZZT/letter-shell/blob/master/src/shell_cfg.h)
- 官方 STM32 FreeRTOS 示例：[demo/stm32-freertos/shell_port.c](https://github.com/NevermindZZT/letter-shell/blob/master/demo/stm32-freertos/shell_port.c)
- 官方许可证：[LICENSE](https://github.com/NevermindZZT/letter-shell/blob/master/LICENSE)

## 3. 本工程目录结构

~~~text
stm32f411ceu6_freertos_transplant/
├── Middlewares/
│   └── letter_shell/
│       ├── inc/                   # 上游公共头（不可改）
│       │   ├── shell.h
│       │   ├── shell_cfg.h
│       │   └── shell_ext.h
│       ├── src/                   # 上游核心实现
│       │   ├── shell.c
│       │   ├── shell_cmd_list.c
│       │   ├── shell_companion.c
│       │   └── shell_ext.c
│       └── port/                  # 项目专用配置与端口
│           ├── shell_cfg_user.h   # 被 shell_cfg.h 通过 #include SHELL_CFG_USER 拉取
│           ├── shell_port.c       # USART1 DMA+IDLE + 环形缓冲 + OSAL 任务
│           └── shell_port.h       # 公开 letter_shell_startup / shellUsartIdleHook
├── STM32F411XX_FLASH.ld           # 链接脚本新增 .shellCommand 段
├── cmake/stm32cubemx/CMakeLists.txt  # 新增 letter_shell OBJECT 库
└── Core/Src/
    ├── freertos.c                 # 在 StartDefaultTask 调用 letter_shell_startup
    └── stm32f4xx_it.c             # USART1_IRQHandler 调 shellUsartIdleHook
~~~

- `inc/`、`src/`：上游原样，便于将来升级 letter_shell 时只覆盖这两层。
- `port/`：项目专用，不动上游一行代码即可完成移植。
- shell 包含路径：`Middlewares/letter_shell/inc` 与 `Middlewares/letter_shell/port`（让 `shell_cfg.h` 能找到 `shell_cfg_user.h`）。

## 4. 从零开始的移植步骤

### Step 1：拉取官方源码并分层落位

~~~powershell
git clone --depth 1 https://github.com/NevermindZZT/letter_shell.git $env:TEMP\opencode\letter_shell_src
New-Item -ItemType Directory -Path "Middlewares\letter_shell\inc","Middlewares\letter_shell\src","Middlewares\letter_shell\port" -Force
Move-Item $env:TEMP\opencode\letter_shell_src\src\shell.h, $env:TEMP\opencode\letter_shell_src\src\shell_cfg.h, $env:TEMP\opencode\letter_shell_src\src\shell_ext.h Middlewares\letter_shell\inc
Move-Item $env:TEMP\opencode\letter_shell_src\src\shell.c, $env:TEMP\opencode\letter_shell_src\src\shell_cmd_list.c, $env:TEMP\opencode\letter_shell_src\src\shell_companion.c, $env:TEMP\opencode\letter_shell_src\src\shell_ext.c Middlewares\letter_shell\src
~~~

上游 `shell_ext.h` 引用 `size_t` 但漏了 `<stddef.h>`，在 `inc/shell_ext.h` 顶部补一行：

~~~c
#include <stddef.h>
#include "shell.h"
~~~

除这一行外，`inc/` 和 `src/` 全部保持原样。

### Step 2：写项目专用配置 `port/shell_cfg_user.h`

上游 `shell_cfg.h` 顶部有：

~~~c
#ifdef SHELL_CFG_USER
#include SHELL_CFG_USER
#endif
~~~

只要在编译时定义 `SHELL_CFG_USER="shell_cfg_user.h"`，`shell_cfg.h` 就会先加载我们的配置。所有 `shell_cfg.h` 中的默认宏都用 `#ifndef` 包裹，所以在我们文件里写过的值会覆盖默认，没写的继续用默认。

~~~c
#ifndef __SHELL_CFG_USER_H__
#define __SHELL_CFG_USER_H__

extern unsigned int shellPortGetTickMs(void);

#define     SHELL_TASK_WHILE            1   /* shell 在任务 while 循环中读 */
#define     SHELL_USING_CMD_EXPORT      1   /* 使用 SHELL_EXPORT_CMD 段导出 */
#define     SHELL_USING_LOCK            1   /* 启用输出锁,防止多任务错乱 */
#define     SHELL_ENTER_LF              1   /* 支持 LF 触发回车 */
#define     SHELL_ENTER_CR              1   /* 支持 CR 触发回车 */
#define     SHELL_ENTER_CRLF            0
#define     SHELL_PARAMETER_MAX_NUMBER  8
#define     SHELL_HISTORY_MAX_NUMBER    10  /* 历史命令 10 条 */
#define     SHELL_MAX_NUMBER            3
#define     SHELL_PRINT_BUFFER          256
#define     SHELL_HELP_LIST_USER        1
#define     SHELL_HELP_LIST_VAR         1
#define     SHELL_HELP_LIST_KEY         1
#define     SHELL_GET_TICK()            ((unsigned int)shellPortGetTickMs())

#endif
~~~

`SHELL_GET_TICK` 用于双击 Tab 显示帮助的时间间隔判定和 shell 自动锁定超时；本工程通过端口层 `shellPortGetTickMs()` 经 OSAL tick 转换返回 ms。

### Step 3：实现端口层 `port/shell_port.c`

端口层负责四件事：

1. USART1 DMA + IDLE 接收 → 环形缓冲 → 信号量通知
2. `shell->write` 阻塞发送回串口
3. `shell->read` 从环形缓冲弹出一字节，环空时阻塞信号量
4. `shell->lock / unlock` 用 OSAL 互斥锁保护多任务并发打印

核心数据：

~~~c
#define SHELL_RX_BUF_SIZE       256     /* DMA 单次接收缓冲 */
#define SHELL_RING_SIZE         512    /* 环形缓冲大小 */
#define SHELL_TX_TIMEOUT_MS     200
#define SHELL_TASK_STACK_SIZE   1024
#define SHELL_TASK_PRIORITY     3

static volatile char  s_rxRing[SHELL_RING_SIZE];
static volatile uint16_t s_rxHead, s_rxTail, s_rxCount;
static uint8_t  s_dmaBuf[SHELL_RX_BUF_SIZE];
extern DMA_HandleTypeDef hdma_usart1_rx;   /* 在 usart.c 中定义 */

static osal_sema_handle_t  s_rxSem;
static osal_mutex_handle_t s_shellMutex;
static Shell   s_shell;
static char    s_shellBuffer[512];
~~~

接收中断钩子（在 `USART1_IRQHandler` 中调用）：

~~~c
void shellUsartIdleHook(void)
{
    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_IDLE) == RESET) return;
    __HAL_UART_CLEAR_IDLEFLAG(&huart1);

    uint16_t remain = __HAL_DMA_GET_COUNTER(&hdma_usart1_rx);
    uint16_t rxLen  = SHELL_RX_BUF_SIZE - remain;

    HAL_UART_DMAStop(&huart1);
    for (uint16_t i = 0; i < rxLen; i++) shellRingPush((char)s_dmaBuf[i]);
    shellStartReceive();                  /* 重启 DMA+IDLE */
    if (rxLen > 0) osal_sema_give(s_rxSem); /* 通知 shell 任务 */
}
~~~

shell 读回调（先在临界区弹一个字符，环空再阻塞信号量）：

~~~c
static signed short userShellRead(char *data, unsigned short len)
{
    signed short c;
    if (len == 0) return 0;

    osal_enter_critical();
    c = shellRingPop();
    osal_exit_critical();
    if (c >= 0) { *data = (char)c; return 1; }

    if (osal_sema_take(s_rxSem, OSAL_MAX_DELAY) != 0) return 0;
    osal_enter_critical();
    c = shellRingPop();
    osal_exit_critical();
    if (c < 0) return 0;
    *data = (char)c;
    return 1;
}
~~~

启动入口（在 FreeRTOS 任务上下文调用一次）：

~~~c
void letter_shell_startup(void)
{
    s_rxHead = s_rxTail = s_rxCount = 0;
    osal_sema_binary_create(&s_rxSem);
    osal_mutex_create(&s_shellMutex);

    s_shell.write  = userShellWrite;
    s_shell.read   = userShellRead;
    s_shell.lock   = userShellLock;
    s_shell.unlock = userShellUnlock;

    shellInit(&s_shell, s_shellBuffer, sizeof(s_shellBuffer));
    shellStartReceive();

    osal_task_handle_t taskHandle = NULL;
    (void)osal_task_create("shellTask", shellTaskEntry,
                           SHELL_TASK_STACK_SIZE, SHELL_TASK_PRIORITY,
                           (osal_task_handle_t)&taskHandle, NULL);
}
~~~

注意 `osal_task_create` 第 5 参数是 `osal_task_handle_t` 按值类型，要传**变量地址**才能让底层写入任务句柄。

### Step 4：在链接脚本中加入 `.shellCommand` 段

`SHELL_EXPORT_CMD` 把命令结构体放到名为 `shellCommand` 的段里。GCC 链接器需要显式收集这段并给出起止符号，否则 `shellInit` 找不到命令表。

在 `STM32F411XX_FLASH.ld` 的 `.rodata` 之后插入：

~~~ld
  /* letter_shell 导出命令段,放在 FLASH,并标记起止符号 */
  .shellCommand ALIGN(4) :
  {
    . = ALIGN(4);
    _shell_command_start = .;
    KEEP(*(shellCommand))
    . = ALIGN(4);
    _shell_command_end = .;
  } >FLASH
~~~

`shell.c` 在 `__GNUC__` 分支用 `_shell_command_start / _shell_command_end` 计算命令数量，因此符号名必须一致。

### Step 5：CMake 加入 letter_shell 库

在 `cmake/stm32cubemx/CMakeLists.txt` 中，与 `FreeRTOS` / `os_adapter` 并列添加：

~~~cmake
# letter_shell - 嵌入式 shell 组件
set(LetterShell_Inc_Dirs
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/letter_shell/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/letter_shell/port
)
set(LetterShell_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/letter_shell/src/shell.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/letter_shell/src/shell_cmd_list.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/letter_shell/src/shell_companion.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/letter_shell/src/shell_ext.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/letter_shell/port/shell_port.c
)
~~~

在 `MX_LINK_LIBS` 中追加 `letter_shell`；

在 `stm32cubemx` INTERFACE 库里追加 include 与编译宏：

~~~cmake
target_include_directories(stm32cubemx INTERFACE ${LetterShell_Inc_Dirs})
target_compile_definitions(stm32cubemx INTERFACE
    ${MX_Defines_Syms}
    "SHELL_CFG_USER=\"shell_cfg_user.h\""
)

add_library(letter_shell OBJECT)
target_sources(letter_shell PRIVATE ${LetterShell_Src})
target_link_libraries(letter_shell PUBLIC stm32cubemx)
~~~

`SHELL_CFG_USER="shell_cfg_user.h"` 这个宏值会被原样传给预处理器，`#include SHELL_CFG_USER` 等价于 `#include "shell_cfg_user.h"`。

### Step 6：在 FreeRTOS 任务中启动 shell

`Core/Src/freertos.c` 的 `StartDefaultTask` 改成一次性启动器：

~~~c
#include "shell_port.h"
#include "shell.h"

void StartDefaultTask(void *argument)
{
  letter_shell_startup();
  osDelay(100);
  shellRun(shellGetCurrent(), "help\r");  /* 启动后内置跑一次 help 自检 */
  osThreadExit();                          /* 自身销毁 */
}

/* 示例自定义命令 */
static int userCmdHello(int argc, char *argv[])
{
  if (argc > 1) shellPrint(shellGetCurrent(), "hello %s!\r\n", argv[1]);
  else          shellPrint(shellGetCurrent(), "hello world!\r\n");
  return 0;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 hello, userCmdHello, say hello);
~~~

### Step 7：USART1 中断挂接 IDLE 钩子

`Core/Src/stm32f4xx_it.c` 的 `USART1_IRQHandler`：

~~~c
#include "shell_port.h"

void USART1_IRQHandler(void)
{
  shellUsartIdleHook();      /* 必须在 HAL_UART_IRQHandler 之前判断 IDLE */
  HAL_UART_IRQHandler(&huart1);
}
~~~

`shellUsartIdleHook` 先判 IDLE 标志是因为 HAL 处理后可能影响标志时序。

## 5. 如何验证移植成功

### 5.1 构建通过

~~~powershell
cmake --preset Debug
cmake --build --preset Debug
~~~

输出应类似：

~~~text
[73/73] Linking C executable stm32f411ceu6_freertos_transplant.elf
Memory region         Used Size  Region Size  %age Used
             RAM:       30280 B       128 KB     23.10%
           FLASH:       55684 B       512 KB     10.62%
~~~

### 5.2 段符号正确

~~~powershell
arm-none-eabi-nm build/Debug/stm32f411ceu6_freertos_transplant.elf | Select-String "_shell_command"
~~~

应看到：

~~~text
0800d620 R _shell_command_end
0800d500 R _shell_command_start
0800d510 R shellCommandclear
0800d5c0 R shellCommandhelp
0800d5d0 R shellCommandusers
...
~~~

`_shell_command_start` 与 `_shell_command_end` 之间存在 shell 命令结构体，说明段导出已生效。

### 5.3 烧录与串口观察

J-Link 烧录：

~~~powershell
& "C:\Program Files\SEGGER\JLink\JLink.exe" -AutoConnect 1 -If SWD -CommanderScript flash.jlink
~~~

`flash.jlink` 内容：

~~~text
si 1
speed 4000
device STM32F411CE
connect
r
h
loadfile build/Debug/stm32f411ceu6_freertos_transplant.elf
r
g
exit
~~~

串口工具连 `COMx` @ 115200 8N1（板子 PA9/PA10），按 RESET，应看到：

~~~text
 _         _   _                  _          _ _
| |    ___| |_| |_ ___ _ __   ___| |__   ___| | |
| |   / _ \ __| __/ _ \ '__| / __| '_ \ / _ \ | |
| |__|  / |_| ||  __/ |    \__ \ | | |  __/ | |
|_____\___|\__|\__\___|_|    |___/_| |_|\___|_|_|
Build:       Aug  5 2026 ...
Version:     3.2.4
Copyright:   (c) 2020 Letter
Command List:
...
hello         say hello
help          ...
letter:/>
~~~

输入 `hello` 回车 → `hello world!`；输入 `hello zhang` → `hello zhang!`。

## 6. 常见问题

| 现象 | 原因 | 处理 |
|---|---|---|
| `error: unknown type name 'size_t'` | `shell_ext.h` 漏 `<stddef.h>` | 在 `inc/shell_ext.h` 顶部加 `#include <stddef.h>` |
| `_shell_command_start undefined` | 链接脚本未加 `.shellCommand` 段 | 在 `.ld` 中加入 `.shellCommand ALIGN(4) :` 段并定义起止符号 |
| `hal_uart_ec error` / `HAL_UART_DMAStop` 返回错误 | 首次启动未先 `HAL_UART_Receive_DMA` 就触发 IDLE | 启动顺序应为 `shellInit` 后再 `shellStartReceive` |
| 串口只见 banner，输入无回显 | TX/RX 接反或终端本地回显开着 | 关闭终端本地回显，让 shell 自己回显 |
| 串口收不到任何数据 | USART1/DMA2_Stream5 中断优先级被 FreeRTOS 屏蔽 | 检查 `HAL_NVIC_SetPriority(USART1_IRQn, 5, 0)` 中断优先级数字 >= `configMAX_SYSCALL_INTERRUPT_PRIORITY` |
| 按方向键出现 `^[[A` 字符 | 终端未发送标准 ANSI 序列或 shell 未识别 | 用 Tera Term/MobaXterm 标准终端；letter_shell 3.2.4 已支持 ANSI 方向键 |
| `osal_task_create` 返回失败 | 任务栈不足或优先级超限 | 调大 `SHELL_TASK_STACK_SIZE`，优先级在 `configMAX_PRIORITIES` 内 |
| 多任务同时打印导致乱码 | 锁未启用 | `shell_cfg_user.h` 设 `SHELL_USING_LOCK 1` 并实现 `lock/unlock` |
| DMA 接收长度总是 0 | `__HAL_DMA_GET_COUNTER` 句柄传错 | 用 `&hdma_usart1_rx`，不是 `huart1.hdmarx`（HAL 内部状态可能不一致）|

## 7. Git 修改范围

~~~text
Middlewares/letter_shell/inc/shell.h
Middlewares/letter_shell/inc/shell_cfg.h
Middlewares/letter_shell/inc/shell_ext.h          (+1: #include <stddef.h>)
Middlewares/letter_shell/src/shell.c
Middlewares/letter_shell/src/shell_cmd_list.c
Middlewares/letter_shell/src/shell_companion.c
Middlewares/letter_shell/src/shell_ext.c
Middlewares/letter_shell/port/shell_cfg_user.h
Middlewares/letter_shell/port/shell_port.c
Middlewares/letter_shell/port/shell_port.h
STM32F411XX_FLASH.ld                              (+.shellCommand 段)
cmake/stm32cubemx/CMakeLists.txt                  (+letter_shell 库)
Core/Src/freertos.c                               (+letter_shell_startup + hello 示例)
Core/Src/stm32f4xx_it.c                           (+shellUsartIdleHook 调用)
~~~

上游 `inc/` 与 `src/` 物理隔离，将来升级 letter_shell 时只覆盖这两层，不动 `port/`。

## 8. 参考资料

- [NevermindZZT/letter-shell 官方仓库](https://github.com/NevermindZZT/letter-shell)
- [letter-shell 官方 README](https://github.com/NevermindZZT/letter-shell/blob/master/README.md)
- [官方 STM32 FreeRTOS 移植示例](https://github.com/NevermindZZT/letter-shell/blob/master/demo/stm32-freertos/shell_port.c)
- [STM32 HAL UART DMA 空闲中断](https://www.st.com/resource/en/user_manual/dm00159893-description-of-stm32f4-hal-and-lowlayer-drivers-stmicroelectronics.pdf)

本工程移植参考公开示例结合自研 OSAL 抽象层完成，所有 shell 命令注册、freeRTOS 任务调度都通过 OSAL 接口而非直接调 RTOS API，便于将来切换 RTOS 时 port 层只改 `os_impl_*`。

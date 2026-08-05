---
title: letter_shell 嵌入式使用手册
date: 2026-08-05
tags:
  - 嵌入式
  - 中间件
  - letter_shell
  - STM32
  - FreeRTOS
  - shell
aliases:
  - letter_shell 使用方法
  - Letter Shell 使用手册
---

# letter_shell 嵌入式使用手册

> [!summary] 适合谁阅读
> 这是一份给第一次接触 letter_shell 的嵌入式开发者的入门手册。你不需要先学会命令行解释器原理，先照着最小示例跑通一个命令即可。

本手册讲“移植完成以后怎么用 letter_shell”。如果你还没有把 letter_shell 放进工程，请先看：

[[letter_shell嵌入式移植指南]]

## 1. letter_shell 到底是什么

letter_shell 是一个跑在 MCU 上的“小命令行”。它让我们在 PC 上通过串口给板子下命令，类似 Linux 终端，但跑在 STM32 里。

它帮我们回答一个问题：

> 这段代码运行后，能不能按照我的指令做事？

比如板子上有个 LED，我们希望输入 `ledon` 后灯亮。用 letter_shell 写成命令就是：

~~~c
static int userCmdLedOn(int argc, char *argv[])
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);
    shellPrint(shellGetCurrent(), "LED ON\r\n");
    return 0;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 ledon, userCmdLedOn, turn on the LED);
~~~

烧录后，串口里输入 `ledon` 回车，板子就点亮 LED 并回 `LED ON`。

这可以读成：

> 我把一个 C 函数注册成 shell 命令，名字叫 `ledon`，串口输入 `ledon`，shell 就调用它。

## 2. 先看一次完整命令

把下面代码放到任意 `.c` 文件（比如 `Core/Src/freertos.c`）：

~~~c
#include "shell.h"

static int userCmdHello(int argc, char *argv[])
{
    if (argc > 1)
        shellPrint(shellGetCurrent(), "hello %s!\r\n", argv[1]);
    else
        shellPrint(shellGetCurrent(), "hello world!\r\n");
    return 0;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 hello, userCmdHello, say hello);
~~~

编译烧录，串口看到：

~~~text
letter:/> hello
hello world!
letter:/> hello zhang
hello zhang!
letter:/>
~~~

这段代码分成四部分：

| 代码                                          | 白话解释                |
|---|---|
| `#include "shell.h"`                          | 使用 letter_shell 的功能  |
| `userCmdHello(int argc, char *argv[])`        | 真正要执行的事           |
| `argc / argv`                                 | 用户传进来的参数         |
| `SHELL_EXPORT_CMD(...)`                       | 把这个函数注册成 shell 命令 |

### 2.1 `argc` 和 `argv` 是什么

- `argc`：用户输入了几个词（含命令名本身）。
- `argv[0]`：命令名（如 `"hello"`）。
- `argv[1]`：第一个参数（如 `"zhang"`）。
- `argv[argc]` 永远是 `NULL`。

输入 `hello zhang` 时：`argc == 2`，`argv[0] == "hello"`，`argv[1] == "zhang"`。

只输入 `hello` 时：`argc == 1`，`argv[0] == "hello"`。

### 2.2 为什么用 `SHELL_EXPORT_CMD` 而不是命令表

letter_shell 支持两种注册方式：

1. **段导出**（推荐）：宏 `SHELL_EXPORT_CMD` 自动把命令结构体放到链接脚本的 `shellCommand` 段，shell 启动时扫描这一段，零样板代码。
2. **命令表**：手工维护一个全局数组。每次加命令都要改表，容易漏。

本工程开了 `SHELL_USING_CMD_EXPORT 1`，只用段导出。新增命令只需在任意 `.c` 文件里写一行宏，链接器自动收集。

## 3. 什么是“命令属性”

`SHELL_EXPORT_CMD` 第一个参数叫“属性”，是一个位域 union。本工程只用到两个位：

| 位                | 含义                                  |
|---|---|
| `SHELL_CMD_PERMISSION(0)` | 权限等级 0~255。0 表示任何登录用户都能调用 |
| `SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)` | 命令类型为 “main 形式” (argc, argv)   |

四种常用命令类型：

| 类型                      | 函数签名                        | 用途           |
|---|---|---|
| `SHELL_TYPE_CMD_MAIN`     | `int func(int argc, char *argv[])`  | 类 main 命令，最常用 |
| `SHELL_TYPE_CMD_FUNC`     | `int func(void)`                    | 无参命令，最简    |
| `SHELL_TYPE_VAR_INT`      | 变量（int）                          | 直接读写变量       |
| `SHELL_TYPE_VAR_STRING`   | 变量（字符串）                        | 直接读写字符串     |

对初学者，记住 `SHELL_TYPE_CMD_MAIN` 一种就够写 90% 的命令。

## 4. 最常用的命令模板

### 4.1 最简命令：无参数

~~~c
static int userCmdReboot(void)
{
    shellPrint(shellGetCurrent(), "rebooting...\r\n");
    HAL_Delay(100);
    NVIC_SystemReset();
    return 0;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC),
                 reboot, userCmdReboot, software reset);
~~~

注意 `SHELL_TYPE_CMD_FUNC` 的函数没有 `argc/argv`，固定写 `void`。

### 4.2 带参数命令：读 ADC

~~~c
static int userCmdAdc(int argc, char *argv[])
{
    if (argc < 2) {
        shellPrint(shellGetCurrent(), "usage: adc <channel>\r\n");
        return -1;
    }
    int ch = atoi(argv[1]);
    uint32_t val = read_adc_channel(ch);
    shellPrint(shellGetCurrent(), "adc[%d] = %lu\r\n", ch, val);
    return 0;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 adc, userCmdAdc, read adc channel);
~~~

`atoi(argv[1])` 把字符串参数转成整数。letter_shell 不会自动转参，参数全是字符串，由你自己转。

### 4.3 多参数命令

~~~c
static int userCmdGpio(int argc, char *argv[])
{
    if (argc < 3) {
        shellPrint(shellGetCurrent(), "usage: gpio <port> <pin> <0|1>\r\n");
        return -1;
    }
    /* argv[1] = "A", argv[2] = "5", argv[3] = "1" */
    /* ...设置 GPIO... */
    shellPrint(shellGetCurrent(), "set %s%s to %s\r\n", argv[1], argv[2], argv[3]);
    return 0;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 gpio, userCmdGpio, set gpio);
~~~

串口输入 `gpio A 5 1` 时：`argc == 4`，`argv[1] == "A"`，`argv[2] == "5"`，`argv[3] == "1"`。

### 4.4 命令调用其他命令：`shellRun`

有时一个命令想触发另一个命令：

~~~c
static int userCmdAll(void)
{
    shellRun(shellGetCurrent(), "adc 0\r");
    shellRun(shellGetCurrent(), "adc 1\r");
    shellRun(shellGetCurrent(), "ledon\r");
    return 0;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC),
                 all, userCmdAll, run three commands at once);
~~~

`shellRun` 第二个参数必须以 `\r` 或 `\n` 结尾。

### 4.5 导出变量

不用写函数，直接把一个全局变量暴露给 shell：

~~~c
int g_loopInterval = 1000;

SHELL_EXPORT_VAR(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_VAR_INT),
                 interval, &g_loopInterval, loop interval in ms);
~~~

串口里：

~~~text
letter:/> vars
Var List:
interval    1000    loop interval in ms

letter:/> setVar interval 500
interval = 500
~~~

`setVar` 是 letter_shell 内置命令，能直接修改导出变量。

### 4.6 查询并打印 FreeRTOS 任务状态

~~~c
#include "FreeRTOS.h"
#include "task.h"

static int userCmdTasks(int argc, char *argv[])
{
    int n = uxTaskGetNumberOfTasks();
    TaskStatus_t *stats = pvPortMalloc(n * sizeof(TaskStatus_t));
    if (!stats) return -1;

    uint32_t totalRun;
    n = uxTaskGetSystemState(stats, n, &totalRun);
    shellPrint(shellGetCurrent(), "name        state  prio  stack\r\n");
    for (int i = 0; i < n; i++) {
        shellPrint(shellGetCurrent(), "%-10s  %d  %d  %u\r\n",
                   stats[i].pcTaskName,
                   (int)stats[i].eCurrentState,
                   (int)stats[i].uxCurrentPriority,
                   (unsigned)stats[i].usStackHighWaterMark);
    }
    vPortFree(stats);
    return 0;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 tasks, userCmdTasks, list FreeRTOS tasks);
~~~

输入 `tasks` 就能看到所有任务名、状态、优先级、剩余栈。

## 5. 一条命令应该怎样写

推荐固定使用下面四步：

1. **校验参数**：`argc` 不够就打 usage。
2. **执行业务**：调驱动、读寄存器、改变量。
3. **打印结果**：用 `shellPrint` 把数据回给串口。
4. **返回值**：0 成功，非 0 失败（shell 会打印 `Return: -1`）。

模板：

~~~c
static int userCmdXxx(int argc, char *argv[])
{
    if (argc < 2) {
        shellPrint(shellGetCurrent(), "usage: xxx <arg>\r\n");
        return -1;
    }
    int arg = atoi(argv[1]);
    int ret = do_something(arg);
    shellPrint(shellGetCurrent(), "result = %d\r\n", ret);
    return (ret < 0) ? -1 : 0;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 xxx, userCmdXxx, do something);
~~~

## 6. 串口工具怎么连

| 工具        | 平台       | 推荐度 | 备注                          |
|---|---|---|---|
| Tera Term   | Windows    | ★★★★★ | 轻量、稳定，方向键/Tab 全支持    |
| MobaXterm   | Windows    | ★★★★☆ | 一体化，支持 SSH/串口/RTT      |
| PuTTY       | 跨平台      | ★★★☆☆ | 最小，但 ANSI 序列处理一般     |
| VS Code ECM | VS Code    | ★★★☆☆ | 需装扩展，不推荐               |
| Zed 终端    | Zed        | ★★☆☆☆ | 内置终端没串口支持，要外接     |

参数固定：

- 波特率：115200（与 `MX_USART1_UART_Init` 一致）
- 数据位：8
- 停止位：1
- 校验：无
- 流控：无

连接 COM 口前，先到设备管理器确认板子是否枚举成功。如果用 USB-TTL，注意 **USB-TTL 的 TX 接板子 RX，RX 接板子 TX**。

## 7. 内置命令一览

letter_shell 自带的命令（通过 `shell_cmd_list.c`）：

| 命令       | 作用                                   |
|---|---|
| `help`     | 列出所有命令                            |
| `cmds`     | 仅列命令                                |
| `vars`     | 列出导出变量                            |
| `users`    | 列出所有用户                            |
| `keys`     | 列出按键绑定                            |
| `clear`    | 清屏                                   |
| `setVar`   | 修改导出变量（如 `setVar interval 500`）|
| `echo`     | 回显字符串                              |

本工程已配 `SHELL_HELP_LIST_USER 1 / SHELL_HELP_LIST_VAR 1 / SHELL_HELP_LIST_KEY 1`，所以 `help` 会把用户、变量、按键都列出来。

## 8. 按键小抄

| 按键             | 作用                          |
|---|---|
| `Enter`          | 提交命令（CR 或 LF 都识别）       |
| `← / →`          | 光标左右移动                    |
| `↑ / ↓`          | 浏览历史命令（最多 10 条）        |
| `Tab`            | 自动补全命令                    |
| `Tab Tab`        | 双击显示该命令 help            |
| `Backspace`      | 删除光标前一字符                |
| `Ctrl+L`         | 清屏                          |
| `Ctrl+U`         | 删除整行                       |

历史命令条数在 `shell_cfg_user.h` 里 `SHELL_HISTORY_MAX_NUMBER` 控制，本工程设 10。

## 9. shell 启动后没有反应怎么办

按顺序排查：

1. **先看 banner**：复位板子，串口应该立刻打出 `letter_shell` ASCII art。没看到 → 接线/波特率错。
2. **能看见但输入无回显**：把终端的 " 本地回显 (Local Echo)" 关闭。letter_shell 自己会回显，开了本地回显会出现 `hheelllloo`。
3. **回车无反应**：终端发送的换行符必须是 `\r` 或 `\n` 或 `\r\n`。`shell_cfg_user.h` 里 `SHELL_ENTER_LF=1 SHELL_ENTER_CR=1`，Tera Term 默认就支持。
4. **方向键出现 `^[[A`**：终端没发标准 ANSI 序列，或者你用了不支持完整 ANSI 的工具。换 Tera Term。
5. **只收到 banner 第一帧就停**：DMA 启动顺序错。看 `letter_shell_startup` 是不是先 `shellInit` 再 `shellStartReceive`，不能反过来。
6. **多任务并发打印错乱**：`SHELL_USING_LOCK` 没开。本工程已开，只要不在 ISR 里直接调 `shellPrint` 就行。

## 10. 在 freeRTOS 任务里调用 shell 的注意点

1. **shell 任务不是 ISR**：所有 `shellPrint / shellRun` 都要在任务上下文调，不能在 ISR 里调。
2. **ISR 想打印请用 RTT 或 elog**：letter_shell 不是日志系统，它跑在任务里，ISR 直接调会破坏锁。
3. **shell `lock/unlock` 用 OSAL 互斥锁**：递归锁性质允许同一个任务多次 `shellPrint` 不死锁。
4. **shell 任务的栈**：本工程 1024 字节。如果命令函数里有大局部数组，要调大 `SHELL_TASK_STACK_SIZE`。
5. **不要用 `printf` 重定向**：letter_shell 直接调 `HAL_UART_Transmit`，不用 `printf`，所以工程里的 `printf` 重定向不会影响 shell 输出。

## 11. 初学者最容易遇到的问题

| 现象 | 用白话说原因 | 怎么处理 |
|---|---|---|
| 命令没出现在 `help` 列表里 | `SHELL_EXPORT_CMD` 宏没用对 / 链接脚本没加段 | 检查宏属性是否对，`STM32F411XX_FLASH.ld` 是否有 `.shellCommand` 段 |
| 输入命令回车没反应 | 终端发的是 `\r\n` 但 `SHELL_ENTER_CRLF` 关着 | 三个换行宏至少开一个，本工程开 LF + CR |
| `argc > 1` 永远是假 | 用户输入参数时没加空格 | shell 用空格分词，`hello zhang` 才会被切成两个词 |
| `atoi` 返回乱七八糟 | `argv[1]` 不是有效数字 | 在调用前加 `argv[1] ? atoi(argv[1]) : 0`，或者 `strtol` |
| 命令函数不返回 | 函数里有 `while(1)` 或者死循环 | 命令函数必须返回，否则 shell 任务被卡死 |
| 命令函数返回 `-1` | shell 会打印 `Return: -1` | 这个没问题，约定俗成。不想要返回值打印可加 `SHELL_CMD_DISABLE_RETURN` 属性 |
| 多次按方向键终端乱跳 | 历史记录满了 | 调大 `SHELL_HISTORY_MAX_NUMBER`，或关掉历史（设为 0） |
| `shellPrint` 里 `%d` 显示乱码 | 参数类型不匹配 | `shellPrint` 内部就是 `vsnprintf`，类型和格式串要对应 |

## 12. 记住这五句话

1. letter_shell 就是把 C 函数注册成串口命令的小工具。
2. 新命令只需写一个函数 + 一行 `SHELL_EXPORT_CMD` 宏。
3. 参数以字符串形式通过 `argc / argv` 传进来，自己用 `atoi / strtol` 转类型。
4. 命令函数必须能返回，不能在里面死循环，否则 shell 任务被卡死。
5. 想让变量也能被 shell 读写，用 `SHELL_EXPORT_VAR` 宏。

## 13. 继续学习

- [letter_shell 官方仓库](https://github.com/NevermindZZT/letter-shell)
- [letter_shell 官方 README（中文）](https://github.com/NevermindZZT/letter-shell/blob/master/README.md)
- [官方 STM32 FreeRTOS 移植示例](https://github.com/NevermindZZT/letter-shell/blob/master/demo/stm32-freertos/shell_port.c)
- [letter_shell 命令导出宏详解](https://github.com/NevermindZZT/letter-shell/blob/master/src/shell.h)

如果某个命令、宏参数或错误日志看不懂，可以把具体代码和串口输出贴出来，按 " 输入 → 命令 → 函数 → 输出 " 的顺序一起分析。

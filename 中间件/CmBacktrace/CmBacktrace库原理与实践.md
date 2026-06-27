---
tags: [cmbacktrace, middleware, debug, hardfault]
created: 2026-06-02
---

# CmBacktrace 库原理与实践

> CmBacktrace (Cortex Microcontroller Backtrace) — ARM Cortex-M 系列 MCU 错误自动追踪与故障诊断库
>
> 仓库：[armink/CmBacktrace](https://github.com/armink/CmBacktrace) (MIT, 2K+ Stars, v1.4.1)

---

## 一、原理

### 1. 异常捕获 — cmb_fault.S 接管 HardFault

CPU 检测到不可恢复异常时自动：

- **压栈**：`R0-R3, R12, LR, PC, xPSR` → 8 words 推入当前栈
- **查向量表**：取出 `HardFault_Handler` 地址
- **模式切换**：Thread → Handler（使用 MSP）

```asm
HardFault_Handler    PROC
    MOV     r0, lr       ; EXC_RETURN（判断异常前用 MSP 还是 PSP）
    MOV     r1, sp        ; 当前 MSP（指向压栈的寄存器上下文）
    BL      cm_backtrace_fault
Fault_Loop
    BL      Fault_Loop    ; while(1)
    ENDP
```

### 2. 现场解析 — 从栈读取 8 个异常入栈寄存器

```c
regs.saved.r0  = ((uint32_t *)saved_regs_addr)[0];
regs.saved.r1  = ((uint32_t *)saved_regs_addr)[1];
regs.saved.r2  = ((uint32_t *)saved_regs_addr)[2];
regs.saved.r3  = ((uint32_t *)saved_regs_addr)[3];
regs.saved.r12 = ((uint32_t *)saved_regs_addr)[4];
regs.saved.lr  = ((uint32_t *)saved_regs_addr)[5];  // 返回地址
regs.saved.pc  = ((uint32_t *)saved_regs_addr)[6];  // 故障 PC
regs.saved.psr = ((uint32_t *)saved_regs_addr)[7];  // 程序状态字
```

### 3. 故障诊断 — 读取 SCB 故障状态寄存器

| 寄存器 | 地址 | 功能 |
|--------|------|------|
| `CFSR` | 0xE000ED28 | 组合 MMFSR/BFSR/UFSR |
| `HFSR` | 0xE000ED2C | HardFault 状态（FORCED=1 表示下级异常升级） |
| `DFSR` | 0xE000ED30 | Debug 状态 |
| `MMAR` | 0xE000ED34 | MemManage 故障地址 |
| `BFAR` | 0xE000ED38 | Bus 故障地址 |

CFSR 关键位：

```
bit 9  DIVBYZERO — 除零（需 CCR.DIV_0_TRP）
bit 8  UNALIGNED — 非对齐访问
bit 3  STKERR   — 异常入栈时 BusFault（栈指针损坏）
bit 1  PRECISERR — 精确数据总线错误（访问非法地址）
bit 0  UNDEFINSTR — 未定义指令（PC 跑飞）
```

---

## 二、完整调用链路

```
故障发生（除零/非对齐/总线错误）
    │
    ▼
CPU 自动压栈 (R0-R3, R12, LR, PC, xPSR)
    │
    ▼
CPU 查向量表 → 跳转 HardFault_Handler
    │
    ▼
cmb_fault.S: r0=EXC_RETURN, r1=SP → BL cm_backtrace_fault
    │
    ▼
cm_backtrace_fault(fault_handler_lr, fault_handler_sp):
    │
    ├─ 检查 init_ok（已调用 cm_backtrace_init？）
    ├─ 检查 on_fault（防重入）
    ├─ on_fault = true
    │
    ├─ cmb_println("Firmware name: ...")
    │
    ├─ EXC_RETURN bit 2 判断上下文：
    │   ├─ = 0 → 任务模式（PSP）→ 输出 "Fault on thread XXX"
    │   └─ = 1 → Handler/裸机（MSP）→ 输出 "Fault on interrupt/bare metal"
    │
    ├─ dump_stack() — 打印栈内容（可选）
    │
    ├─ 读取并输出寄存器：
    │   ├─ R0~R3, R12, LR, PC, PSR
    │   └─ CFSR/HFSR/DFSR/MMAR/BFAR
    │
    ├─ fault_diagnosis() — 自动诊断：
    │   ├─ DIVBYZERO → "divide by zero"
    │   ├─ UNALIGNED → "unaligned access"
    │   └─ PRECISERR → "precise data access violation"
    │
    └─ print_call_stack() — 栈回溯 → 输出 addr2line 命令
```

---

## 三、栈回溯算法详解

这是 CmBacktrace 最核心的部分——通过扫描栈内存还原函数调用链。

### 算法流程

```python
def backtrace(stack_pointer, stack_start, stack_size, code_start, code_end):
    call_stack = []

    # Step 1: 预填充故障 PC 和 LR
    if on_fault:
        call_stack.append(regs.saved.pc)      # 故障 PC（精确）
        lr_pc = regs.saved.lr - 1              # LR→PC 修正
        if 在代码段内:
            call_stack.append(lr_pc)

    # Step 2: 线性扫描栈空间
    for addr in range(stack_pointer, stack_start + stack_size, 4):
        value = *(uint32_t*)addr               # 读栈上的一个 word
        pc = value - 1                         # 尝试作为返回地址

        # 筛选 1：Thumb 模式 PC 必须为奇数
        if pc % 2 == 0: continue

        # 筛选 2：必须在代码段内
        if pc < code_start or pc > code_end: continue

        # 筛选 3：前序指令必须是 BL/BLX（关键验证）
        if not is_BL_or_BLX(pc - 4): continue

        call_stack.append(pc)

    return call_stack
```

### 为什么 Thumb 模式 PC 必须是奇数？

Cortex-M 只有 Thumb 模式。ARM CPU 使用**返回地址的 bit 0 表示指令集模式**：

```
BL func            → CPU 自动设 LR = 返回地址 | 1
                       ↑           ↑
                       |          实际地址 0x08001004
                       bit 0 = 1 表示 Thumb 模式
```

### 为什么 PC 之前的指令必须是 BL/BLX？

ARM Thumb-2 中，**只有 `BL` 和 `BLX` 指令会设置 LR**（保存返回地址）。其他跳转指令（`B`、`BX` 等）不会。

```
内存：
0x08001000:  BL  funcB         ← 设置 LR = 0x08001005
0x08001004:  MOV r0, #0        ← 返回地址
              ↓ 执行
funcB:        PUSH {LR}        ← 0x08001005 入栈
              ...
              POP  {PC}        ← 恢复 0x08001005 → 跳回 0x08001004

逆向验证：
栈上找到 0x08001005
  → pc = 0x08001005 - 1 = 0x08001004
  → 读取 0x08001000 处的指令 = 0xF000F008 = BL funcB ✅
  → 确认是函数调用返回地址
```

### 指令编码检查

```c
static bool disassembly_ins_is_bl_blx(uint32_t addr) {
    uint16_t ins1 = *(uint16_t*)addr;       // 低 16 位
    uint16_t ins2 = *(uint16_t*)(addr + 2);  // 高 16 位

    // BL: 1111 0xxx xxxx xxxx | 1111 1xxx xxxx xxxx
    if ((ins2 & 0xF800) == 0xF800 && (ins1 & 0xF800) == 0xF000)
        return true;

    // BLX: 0100 0111 xxxx xxx0
    if ((ins2 & 0xFF00) == 0x4700)
        return true;

    return false;
}
```

### 算法局限性

| 局限 | 原因 | 影响 |
|------|------|------|
| 数据巧合命中 | 变量值恰好在代码段 +BL 后 | 概率低，可接受 |
| 内联函数 | 编译器优化后无独立 BL | 调试建议 `-O0` |
| 尾调用优化 | 函数末尾直接跳转不保存 LR | 丢失一级调用链 |
| 栈溢出 | 栈指针超范围 | 检测后重置扫描起点 |

---

## 四、API 参考

### cm_backtrace_init()

```c
void cm_backtrace_init(
    const char *firmware_name,  // 固件名（与 .axf 同名便于 addr2line）
    const char *hardware_ver,   // 硬件版本
    const char *software_ver    // 软件版本
);
```

必须在 UART/RTT 初始化后、任何可能触发故障的代码前调用。

### cm_backtrace_fault()

```c
void cm_backtrace_fault(
    uint32_t fault_handler_lr,  // EXC_RETURN
    uint32_t fault_handler_sp   // 故障入口 SP
);
```

由 cmb_fault.S 自动调用，一般不需手动调。

### cm_backtrace_assert()

```c
void cm_backtrace_assert(uint32_t sp);
```

在 `assert_failed()` 中调用，SP 尽量在函数入口处获取。

### cm_backtrace_call_stack()

```c
size_t cm_backtrace_call_stack(
    uint32_t *buffer,     // 输出缓冲区
    size_t size,          // 缓冲区深度
    uint32_t sp           // 栈指针
);
```

可在正常运行状态下主动获取调用栈。

---

## 五、数据结构

### cmb_hard_fault_regs

```c
struct cmb_hard_fault_regs {
    struct {
        uint32_t r0, r1, r2, r3;
        uint32_t r12;
        uint32_t lr;
        uint32_t pc;
        union {
            uint32_t value;
            struct { /* V, C, Z, N 等位域 */ } bits;
        } psr;
    } saved;

    uint32_t syshndctrl;   // 0xE000ED24
    uint8_t  mfsr;          // 0xE000ED28 (MemManage)
    uint32_t mmar;          // 0xE000ED34
    uint8_t  bfsr;          // 0xE000ED29 (Bus)
    uint32_t bfar;          // 0xE000ED38
    uint16_t ufsr;          // 0xE000ED2A (Usage)
    uint32_t hfsr;          // 0xE000ED2C
    uint32_t dfsr;          // 0xE000ED30
    uint32_t afsr;          // 0xE000ED3C
};
```

---

## 六、配置宏

| 宏 | 作用 | 典型值 |
|----|------|--------|
| `cmb_println(...)` | 输出通道（**必须配置**） | `printf(...)` 或 `snprintf+SEGGER_RTT_Write` |
| `CMB_USING_BARE_METAL_PLATFORM` | 裸机模式 | 定义/注释 |
| `CMB_USING_OS_PLATFORM` | OS 模式 | 与裸机二选一 |
| `CMB_OS_PLATFORM_TYPE` | 操作系统类型 | `CMB_OS_PLATFORM_FREERTOS` |
| `CMB_CPU_PLATFORM_TYPE` | CPU 架构 | `CMB_CPU_ARM_CORTEX_M4` |
| `CMB_USING_DUMP_STACK_INFO` | 是否 Dump 栈内容 | 调试阶段开启 |
| `CMB_PRINT_LANGUAGE` | 语言 | `CMB_PRINT_LANGUAGE_ENGLISH` |
| `CMB_CALL_STACK_MAX_DEPTH` | 调用栈最大深度 | 16 |
| `CMB_CSTACK_BLOCK_NAME` | Keil 主栈段名 | `STACK` |

---

## 七、移植验证实战结论

### 关键配置

| 参数 | 推荐值 | 说明 |
|------|--------|------|
| CSTACK | **4KB** | 故障处理 + snprintf 需要充足栈空间 |
| RTT BUFFER_SIZE_UP | **4KB** | 一次性故障输出可能超 1KB |
| cmb_println | `snprintf + SEGGER_RTT_Write` | 比 `SEGGER_RTT_printf` 更安全 |
| vTaskStackSize | 固定值 | FreeRTOS V10.3.1 TCB 无栈深字段 |
| CMB_ASSERT | `((void)(EXPR))` | Keil ARMCC 下 BSS 初始化竞态 |

### 常见陷阱

```c
// 1. 除零不触发 HardFault → 手动使能
SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;

// 2. 故障测试应在 FreeRTOS 任务中，非 main()
// main() 中触发 → "Fault on bare metal"
// 任务中触发 → "Fault on thread defaultTask" ✅

// 3. 先打开 RTT Viewer/串口，再复位板子

// 4. addr2line 需要：
//    - Keil Debug Information 已勾选
//    - .axf 调试符号未 strip
```

### 输出示例

```
Firmware name: STM32F411_CmBacktrace_Test, hardware version: HW-1.0, software version: SW-1.0.0
Fault on thread defaultTask
  R0 : 00000064  R1 : 00000000  R2 : a5a5a5a5  R3 : a5a5a5a5
  R12: a5a5a5a5  LR : 080051c9  PC : 0800194a  PSR: 41000000
Usage fault is caused by Indicates a divide by zero has taken place
Show more call stack info by run: addr2line -e firmware.axf -afpiC 0800194a 080051c8
```

---

## 八、参考

- 官方仓库：https://github.com/armink/CmBacktrace
- STM32 移植指南：[[CmBacktrace_STM32移植指南]]
- 同作者项目：SFUD（[[SFUD库]]）、EasyLogger（[[EasyLogger库]]）、EasyFlash
- 关联技能：[[cmbacktrace-debug]]

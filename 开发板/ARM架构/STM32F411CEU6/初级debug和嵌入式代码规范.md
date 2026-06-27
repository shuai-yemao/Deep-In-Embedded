日期：2026.3.13

文章标签： #STM32 #debug #代码规范

## 1. 学习内容

### 知识点总览

| 序号  | 知识点     |
| --- | ------- |
| 1   | 初级调试    |
| 2   | 嵌入式代码规范 |

### 知识点关联思维导图

![[assets/初级debug和嵌入式代码规范/file-20260421201612428.png]]

---

## 2. 逐点精讲

### 知识点 1：初级调试

#### 辅助图示

1. 调试器与 CPU 内部连接框图 ![[assets/初级debug和嵌入式代码规范/file-20260421201611430.png]]
2. 编译器生成代码过程 ![[assets/初级debug和嵌入式代码规范/file-20260421201611427.png]]![[assets/初级debug和嵌入式代码规范/file-20260421201612425.png]]
3. Arm 内核框图 ![[assets/初级debug和嵌入式代码规范/file-20260421201611424.png]]
4. 数据观察点窗口 ![[assets/初级debug和嵌入式代码规范/file-20260421201612415.png]]
5. 系统寄存器窗口 ![[assets/初级debug和嵌入式代码规范/file-20260421201612358.png]]
6. 输出反汇编文件 ![[assets/初级debug和嵌入式代码规范/file-20260421201612419.png]]
7. Listing 窗口设置 map 文件位置 ![[assets/初级debug和嵌入式代码规范/file-20260421201612421.png]]

#### 核心逻辑/原理

调试器通过芯片的调试总线（DAP）连接到芯片内核的总线矩阵中去访问芯片内部各寄存器地址下的数据

#### 关键公式/结论

1. 常用的 JTAG+SWD 都是侵入式调试会影响系统运行
2. 了解下载代码的流程对了解底层的调试有帮助
3. 工程中的 Map 文件描述了代码保存位置和变量位置等等，局部变量因为在栈中运行，在 map 中是看不到的，可以使用数据窗口 debug 查看，map 文件通过双击 keil 项目名称即可打开（需要在魔术棒中设置 listing 窗口的 select folder for listings 到工程文件夹中）
4. 可以通过 system view 窗口观察系统寄存器状态，keil 通过. Sfd 文件来了解各寄存器状态，. Sfd 文件中描述了各系统寄存器地址的含义及其定位
5. ==Volatile 在汇编层面的作用是告诉编译器去 map 文件中变量的真实地址读取和操作变量，而不是从寄存器中==
6. Beyond compare 软件通过比较文件来快速定位问题，白色显示为相同部分，红色表示两者有差异的部分
7. Ai 在调试中的作用是解释汇编语言以及分析代码中 bug

### 知识点 2：嵌入式代码规范

#### 实际意义

一份优美的代码同时包含了：对业务问题的理解、对语言特性的理解和运用，良好的编程习惯，这是一个程序员编程功底的综合体现

#### 关键公式/结论

1. 头文件先系统头文件，后中间件头文件，最后是自定义头文件，C 语言微库要用<>来引用
2. 函数和头文件都要注释，头文件说明文件作者，创建时间，功能；函数说明参数，作用，以及返回值
3. 枚举变量名小写，以下划线 t 结尾，内部枚举统一大写
4. 宏定义全部大写，用下划线隔开
5. 全局变量以 g_ 开头
6. 指针变量 p_ 开头
7. 函数指针变量 pf_ 开头
8. 任何函数调用，必须判断其返回值
9. 任何指针使用前都需要判断是否为空指针
10. 判读语法中必须把常量放在 == 左边
11. 代码的宽度不得超过 80 列
12. 头文件命名按项目名称 + 模块名称
13. 函数名称为项目名称 + 模块 + 功能

- ---

## 3. 相关资料

### 🎥 视频链接

[【嵌入式程序的第一节课】五百强大厂工程师带你学代码规范和架构_哔哩哔哩_bilibili](https://www.bilibili.com/video/BV1aJ4m1b7U2/?share_source=copy_web&vd_source=15bad2bcd085cfc0439f4c8d50ecb9b5)

[【嵌入式的第一节课】嵌入式头文件才是架构师之路_哔哩哔哩_bilibili](https://www.bilibili.com/video/BV1qt421t7vy/?share_source=copy_web&vd_source=15bad2bcd085cfc0439f4c8d50ecb9b5)

### 🔗 资料链接

[FreeRTOS 的发行 | 掌握FreeRTOS™实时内核](https://freertoskernel.asicfans.com/freertos-de-fa-hang)

[MISAR C 2012](https://st-eit.notion.site/MISAR-C-2012-c3444949996c432da1588f604acf6b31)

[MISRA C:2012 嵌入式C规范解读 | 普通人](https://hjk.life/posts/misra-c-2012/#%E6%9C%AF%E8%AF%AD%E4%B8%AD%E8%8B%B1%E8%A1%A8)

[keil无法查看外设寄存器（生成SFR文件）_keil sfr文件-CSDN博客](https://blog.csdn.net/weixin_51686526/article/details/137756385)

[‌‬﻿‬‬​​​​​‌‍‬​﻿‬​​⁠​​​﻿‍​​​​﻿​​‬​​‍﻿‬​‍‍‬﻿​﻿‬​‬手把手讲解调试原理与方案 - 飞书云文档](https://twd6onxsxva.feishu.cn/docx/Yn3zddPRnoyVlaxIXVBcuXlZnne)

[⁠​​​‌​⁠​‬​﻿‍‍﻿⁠​﻿​‬﻿‬​﻿​⁠​﻿⁠​​​‍​​﻿‍‌‌﻿​​⁠‌⁠​​⁠﻿7_嵌入式代码规范 - 飞书云文档](https://twd6onxsxva.feishu.cn/docx/YlrSdxAuuozytPxq6jXcgiWIn7g)

[‍⁠​﻿​​‍​​​‌‌‌​‌‬﻿​﻿‍​​​​‍⁠﻿​​​​​​​​﻿﻿​​‬‍​​‍​​​​​​12 C编译过程（compiler） - 飞书云文档](https://twd6onxsxva.feishu.cn/docx/PH7bdmU62oXmfZxxgl0cPgWmn9b)

[计算机那些事(5)——链接、静态链接、动态链接 | 楚权的世界](https://chuquan.me/2018/06/03/linking-static-linking-dynamic-linking/#%E9%93%BE%E6%8E%A5%E6%A6%82%E8%BF%B0)

[keil的debug界面了解与使用](https://blog.csdn.net/MANONGDKY/article/details/149498857)

[keil的串口调试工具的使用](https://jishuzhan.net/article/2044653105317478401)

### 💻 代码/PDF

[[Cortex M3与M4权威指南.pdf]]

---

## 4. Q&A

### Q 1: 程序的编译过程分几个部分？

 A 1: 编译过程分为预处理、编译、汇编和链接，c 文件通过预处理去除注释，展开宏定义和头文件，同时进行条件编译得到了 i 文件也叫编译单元；i 文件通过编译将 c 语言翻译成汇编语言 s 文件；s 文件通过汇编器将汇编语言转换成二进制文件 o 文件；最后通过链接将多个模块中目标代码合并成一个 exe 可执行文件

### Q 2 ：宏定义是在编译的哪个阶段被处理的？

A 2: 预处理阶段

### Q 3：怎么用预处理指令 # define 声明一个常数，用以表明 1 年中有多少秒（忽略闰年问题）？

A 3: # define PI 3.1415

### Q 4：预处理标识 # error 的目的是什么？

A 4: # error 会在编译遇到问题时跳出编译错误，保证程序按你所想编译

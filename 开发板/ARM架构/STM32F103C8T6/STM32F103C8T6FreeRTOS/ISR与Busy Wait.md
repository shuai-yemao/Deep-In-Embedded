ISR(中断服务程序)

BusyWait(不断轮询某一个东西的概念)

![[assets/ISR与Busy Wait/file-20260421201630032.png]]

串行逻辑，在实时操作系统中会导致 bug，因为逻辑是等待倒车雷达的回应，无回应则卡死在此处

![[assets/ISR与Busy Wait/file-20260421201630040.png]]

中断程序中一般都是定时读取任务，主函数中存放执行，同时要确定好中断程序的优先级，这也就是前后台线程

![[assets/ISR与Busy Wait/file-20260421201630043.png]]

RTOS 的 CPU 通过快速切换线程来模拟并行逻辑

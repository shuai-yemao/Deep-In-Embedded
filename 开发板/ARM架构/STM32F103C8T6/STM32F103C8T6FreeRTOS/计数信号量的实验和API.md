定义一个计数信号量并且确定信号量的最大值 ![[assets/计数信号量的实验和API/file-20260421201646097.png]]

创建一个计数信号量

![[assets/计数信号量的实验和API/file-20260421201646100.png]]

形参为信号量最大值和信号量初始值，返回值为队列结构体指针

![[assets/计数信号量的实验和API/file-20260421201646104.png]]

通过信号量获取函数来判断信号量是否被占用，uxSemaphoreGetConut 函数为计数信号量获取函数，形参为信号量句柄，返回值为 UBaseType_t 就是 unsigned long，fflush(stdout) 是清空输出缓冲区

#### 定时器简介
定时器是单片机内部集成的功能，它能够通过编程进行灵活控制。
**单片机的定时功能依赖于内部的计数器实现**，每当单片机经历一个机器周期并产生一个脉冲时，计数器就会递增。
**定时器的主要作用在于计时**，当设定的时间到达后，它会触发中断，从而通知系统计时完成。
在中断服务函数中，我们可以编写特定的程序以实现所需的功能。

#### 硬件定时器和软件定时器
硬件定时器
	依托微控制器的内置硬件机制，通过专门的计时/计数器电路达成定时功能。**其显著优势在于高精度与高可靠性。**
	因为硬件定时器的工作独立于软件任务和操作系统调度
[[软件定时器]]
	通过操作系统或软件库模拟实现的定时功能。这类定时器的性能受系统当前负载和任务调度策略制约，因此在精度上较硬件定时器稍逊一筹。
	适用于对时间控制要求不那么严格的场景
#### ESP-TIMER函数解析
必要头文件#include "esp_timer.h"

esp_err_t esp_timer_create(const esp_timer_create_args_t* create_args,esp_timer_handle_t* out_handle);
	该函数用于创建 ESPTIMER 实例
	参数表
		![[Pasted image 20250910195907.png]]
	 esp_timer_create_args_t 结构体的成员变量描述
		 ![[Pasted image 20250910195927.png]]

esp_err_t IRAM_ATTR esp_timer_start_periodic(esp_timer_handle_t timer,uint64_t period_us);
	该函数用于使能定时器的指定中断
	参数表
		![[Pasted image 20250910200110.png]]
		


### 通用定时器简介
基本的定时器参数设置包括定时器号、通道号、预分频器配置、自动重新加载值的设定，以及定时器中断使能功能的开启。
### 定时器的基础配置
![[Pasted image 20250926193837.png]]
每个定时器可通过配置寄存器TIMG_TxCONFIG_REG 的 TIMG_Tx_USE_XTAL 字段，选择 APB 时钟(APB_CLK)或外部时钟(XTAL_CLK)作为时钟源

16 位预分频器的分频系数可通过 **TIMG_Tx_DIVIDER** 字段配置，选取从 2 到 65536 之间的任意值（注意，
将 TIMG_Tx_DIVIDER 置 0 后，分频系数会变为 65536。TIMG_Tx_DIVIDER 置 1时，实际分频系数为 2，计数器的值为实际时间的一半。）

54 位时基计数器基于 TB_CLK，可通过**TIMG_Tx_INCREASE 字段配置为递增或递减**。时
基计数器可通过置位或**清零 TIMG_Tx_EN 字段使能或关闭。**

**时基计数器 54 位定时器的当前值必须被锁入两个寄存器，才能被 CPU 读取（因为 CPU 为32 位）。**

定时器可配置为在当前值与报警值相同时触发报警。报警会产生中断，4 位 报 警 值 可 在**TIMG_TxALARMLO_REG 和TIMG_TxALARMHI_REG 配置，两者分别代表报警值的低 32 位和高 22 位**。但是，只有置位**TIMG_Tx_ALARM_EN 字段使能报警功能**后，配置的报警值才会生效。

### 通用定时器函数解析
需要导入必要的头文件：
#include "driver/gptimer.h"

esp_err_t gptimer_new_timer(const gptimer_config_t *config,gptimer_handle_t *ret_timer);
	该函数用于配置通用定时器
	参数表
		![[Pasted image 20250926194612.png]]
		intr_shared参数可以使不同的定时器中断可以在同一个中断服务函数中进行处理。
esp_err_t gptimer_set_raw_count(gptimer_handle_t timer,unsigned long long value);
	该函数用于配置通用定时器的计数值以及定时器周期
	参数表
		![[Pasted image 20250926195006.png]]
esp_err_t gptimer_register_event_callbacks(gptimer_handle_t timer,const gptimer_event_callbacks_t *cbs,void *user_data);
	该结构体用于注册用户回调函数
	参数表
		![[Pasted image 20250926195155.png]]
esp_err_t gptimer_set_alarm_action(gptimer_handle_t timer,const gptimer_alarm_config_t *config);
	 该函数用于配置通用定时器报警事件
	 参数表
		 ![[Pasted image 20250926195356.png]]
esp_err_t gptimer_enable(gptimer_handle_t timer);
	该函数用于使能定时器
esp_err_t gptimer_start(gptimer_handle_t timer);
	该函数用于启动定时器


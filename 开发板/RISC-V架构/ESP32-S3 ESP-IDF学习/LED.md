### LED简介
LED 驱动方式主要有恒流和恒压两种，其中，恒流驱动因其能限定电流而备受青睐.
### LED驱动方式
灌入电流接法。
	指的是 LED 的供电电流是由外部提供电流，将电流灌入我们的 MCU；
	![[Pasted image 20250909173403.png]]
输出电流接法。
	指的是由 MCU 提供电压电流，将电流输出给 LED；如果使用 MCU的 GPIO 直接驱动 LED，则驱动能力较弱，可能无法提供足够的电流驱动 LED。
	![[Pasted image 20250909173845.png]]
### GPIO函数解析
必要头文件#include “driver\gpio.h”
esp_err_t gpio_config(const gpio_config_t *pGPIOConfig)
	该函数用来配置 GPIO 的模式、上下拉等功能，其函数原型如下所示：
	```
	/* GPIO 配置参数 */
	typedef struct {
	   uint64_t pin_bit_mask; /* 配置引脚位 */
	   gpio_mode_t mode; /* 设置引脚模式 */
	   gpio_pullup_t pull_up_en; /* 设置上拉 */
	   gpio_pulldown_t pull_down_en; /* 设置下拉 */
	   gpio_int_type_t intr_type; /* 中断配置 */
	 } gpio_config_t;
	```
	参数表
		![[Pasted image 20250909174159.png]]
esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level);
	该函数用于配置某个管脚输出电平
	 参数配置
		![[Pasted image 20250909174519.png]]
		返回值：ESP_OK 表示设置成功，ESP_FAIL 表示设置失败。
esp_err_t gpio_get_level(gpio_num_t gpio_num);
	该函数用于获取某个管脚的电平
	![[Pasted image 20250909174604.png]]
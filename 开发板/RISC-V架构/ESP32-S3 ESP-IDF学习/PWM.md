### PWM简介
PWM（Pulse Width Modulation），简称脉宽调制，**是一种将模拟信号变为脉冲信号的计数。**
### PWM的参数
- PWM 周期是一个 PWM 信号的时间；
- 脉宽时间是指高电平时间；
- 脉宽时间占 PWM 周期的比例就是占空比。
- 例如，如果 PWM 周期是 10ms，而脉宽时间为 8ms，那么 PWM 占空比就是8/10=80%，此时的 PWM 信号就是占空比为 80%的 PWM 信号。
- PWM 名为脉冲宽度调制，顾名思义，就是通过调节 PWM 占空比来调节 PWM 脉宽时间。

### PWM函数解析
建 议 先 配 置 定 时 器(调 用 函 数ledc_timer_config())，再配置通道(调用函数 ledc_channel_config())。这样可以确保 IO 引脚上的
PWM 信号自输出开始那一刻起，其频率就是正确的。

esp_err_t ledc_timer_config(const
ledc_timer_config_t *timer_conf);
	配置pwm输出的参数和定时器
	![[Pasted image 20250926201106.png]]
esp_err_t ledc_channel_config(const ledc_channel_config_t *ledc_conf);
	配置pwm通道
	![[Pasted image 20250926201404.png]]
esp_err_t ledc_set_duty(ledc_mode_t speed_mode,
ledc_channel_t channel,uint32_t duty);
	调用函数 ledc_set_duty()可以设置新的占空比。之后，调用函数 ledc_update_duty()使新配置
	生效。要查看当前设置的占空比，可使用 get 函数 ledc_get_duty()，
	参数表
		![[Pasted image 20250926201518.png]]
esp_err_t ledc_update_duty(ledc_mode_t speed_mode, ledc_channel_t channel);
	在上一步调用 ledc_set_duty()设置新的占空比后，调用函数 ledc_update_duty()使新配置生效
	参数表
	![[Pasted image 20250926201705.png]]
esp_err_t ledc_fade_func_install(int intr_alloc_flags);
	[[LED]] PWM 控制器硬件可逐渐改变占空比的数值。 开 启 此 功 能 ， 需 要 用 函 数edc_fade_func_install()使能渐变
	![[Pasted image 20250926202135.png]]
esp_err_t ledc_set_fade_with_time(ledc_mode_t speed_mode, ledc_channel_t channel,uint32_t target_duty,int max_fade_time_ms);
	经过上一步渐变功能的配置后，需要设置占空比以及渐变时长
	参数表
		![[Pasted image 20250926202210.png]]
esp_err_t ledc_fade_start(ledc_mode_t speed_mode,
 ledc_channel_t channel,
 ledc_fade_mode_t fade_mode);
	 设置占空比以及渐变时长后，便可开启渐变功能
	 参数表
		 ![[Pasted image 20250926202343.png]]

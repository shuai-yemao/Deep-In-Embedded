tick是系统定时器产生的固定周期的一个tick中断时间，tick中断发生后，系统RT0S会更新内部的时间基准，同时会进行任务的切换，tick=1000，也就是1ms切换一次任务![[Pasted image 20250628160539.png]]（在freertosconfig头文件中修改和查看）
这里显示tick为1ms，1/1000
TickType_t 类型为unsigned int 在STM32中是32位，计时时间是2^32/1000(系统嘀嗒计时器频率)约定于49天，超过阈值后数值清零![[Pasted image 20250628152542.png]]
利用无符号类型的溢出特性
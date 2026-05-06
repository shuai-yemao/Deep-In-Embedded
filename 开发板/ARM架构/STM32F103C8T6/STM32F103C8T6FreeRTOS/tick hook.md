tick hook是tick中断的回调函数
特点：
	![[Pasted image 20250628161305.png]]
	configUSE_TICK_HOOK置一
![[Pasted image 20250628161618.png]]
产生系统中断的核心函数‘
![[Pasted image 20250628162305.png]]
vPortRaiseBASEPRI函数是临时提高中断屏蔽寄存器的等级提到最高优先级，防止被其他任务打断
xTaskIncrementTick函数是更新系统的tick时间，检查是否有任务延时到期，有任务切换就返回pdTURE，没有就返回pdFALSE
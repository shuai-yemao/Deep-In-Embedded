tick hook是tick中断的回调函数
特点：
	![[assets/tick hook/file-20260421201632922.png]]
	configUSE_TICK_HOOK置一
![[assets/tick hook/file-20260421201632926.png]]
产生系统中断的核心函数‘
![[assets/tick hook/file-20260421201632929.png]]
vPortRaiseBASEPRI函数是临时提高中断屏蔽寄存器的等级提到最高优先级，防止被其他任务打断
xTaskIncrementTick函数是更新系统的tick时间，检查是否有任务延时到期，有任务切换就返回pdTURE，没有就返回pdFALSE

日期：2026.4.22

文章标签： #bootloader #OTA

## 1. 学习内容

### 知识点总览

| 序号  | 知识点 |
| --- | --- |
| 1   |     |
| 2   |     |
| 3   |     |
| 4   |     |

### 知识点关联思维导图

---

## 2. 逐点精讲

### 知识点 1 Z

#### 实际意义

#### 应用场景

#### 常见误区

#### 辅助图示

1. Bootloader 流程 ![[file-20260429204733682.png]]

#### 通俗人话解释

#### 核心逻辑/原理

#### 关键公式/结论

1. ../Drivers/STM32F4xx_StdPeriph_Driver/src/misc.c(157): error: no member named 'IPR' in 'NVIC_Type'157 |     NVIC->IPR[NVIC_InitStruct->NVIC_IRQChannel] = tmppriority; 移植工程报错将 IPR 改成 IP 即可
2. 

- ---

## 3. 相关资料

### 🎥 视频链接

### 🔗 资料链接

[STM32实现bootloader跳转的关键步骤](https://zhuanlan.zhihu.com/p/648855822#:~:text=%E6%9C%80%E7%AE%80%E5%8D%95%E7%9A%84%E4%B8%80%E7%A7%8D%E5%8D%87%E7%BA%A7%E6%96%B9%E6%A1%88%E6%98%AF%EF%BC%9A%E4%B8%80%E4%B8%AA%20BootLoader%20%E5%92%8C%20%E4%B8%80%E4%B8%AA%20APP%20%EF%BC%8CBootLoader%20%E5%AE%9E%E7%8E%B0%E8%B7%B3%E8%BD%AC%E5%92%8C%E5%8D%87%E7%BA%A7APP%20%E7%9A%84%E5%8A%9F%E8%83%BD%E3%80%82,0x10000%EF%BC%8C64K%20%E5%AD%97%E8%8A%82%E3%80%82%20APP%20%E5%AD%98%E5%82%A8%E7%9A%84%E5%9C%B0%E5%9D%80%EF%BC%8C%E5%AE%89%E6%8E%92%E5%9C%A8%20BootLoader%20%E5%90%8E%E8%BE%B9%EF%BC%8C%E5%8D%B3%E5%AD%98%E5%82%A8%E5%9C%B0%E5%9D%80%E4%B8%BA%200x8010000%EF%BC%8CFlash%20%E5%89%A9%E4%BD%99%E7%9A%84%E7%A9%BA%E9%97%B4%E9%83%BD%E5%8F%AF%E4%BB%A5%E5%88%86%E9%85%8D%E7%BB%99APP%E3%80%82)

[Ymodem协议详解](https://blog.csdn.net/huangdenan/article/details/103611081)

[SecureCRT连接串口教程](https://blog.csdn.net/weixin_43738690/article/details/113398519)

### 💻 代码/PDF

```
#include "boot_manager.h"

void JumpToApp(void)
{
	/*检查栈顶地址是否合法*/
	uint32_t JumpAddress;
	pFunction Jump_To_Application;
	/*检查栈顶地址是否合法*/
	if(((*(__IO uint32_t *)ApplicationAddress) & 0x2FFE0000) == 0x20000000)
	{
		/*屏蔽所有中断，防止在跳转过程中，中断干扰出现异常*/
		__disable_irq();
		NVIC_SetVectorTable(NVIC_VectTab_FLASH, 0x10000);
		RCC_DeInit();
	
		/*用户代码区第二个字为程序开始地址（复位地址）*/
		JumpAddress = *(__IO uint32_t *) (ApplicationAddress + 4);
	
		/* Initialize user application's Stack Pointer */
		/*初始化APP堆栈指针（用户代码区的第一个字用于存放栈顶地址）*/
		__set_MSP(*(__IO uint32_t *) ApplicationAddress);
		
		/*类型转换*/
		Jump_To_Application = (pFunction) JumpAddress;
		
		/*跳转到 APP*/
		Jump_To_Application();
	}
}

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __BOOT_MANAGER_H
#define __BOOT_MANAGER_H

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Exported types ------------------------------------------------------------*/
typedef void (*pFunction) (void);
/* Exported constants --------------------------------------------------------*/
/* Exported macro ------------------------------------------------------------*/
#define ApplicationAddress	0x8008000
#define NVIC_VectTab_FLASH	((uint32_t)0x08000000)

/* Exported functions ------------------------------------------------------- */
void JumpToApp(void);

#endif /* __MAIN_H */
```

---

## 4. Q&A

### Q 1

 A 1:

### Q 2

A 2:

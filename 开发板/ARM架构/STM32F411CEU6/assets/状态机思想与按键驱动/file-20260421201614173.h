/******************************************************************************
 * Copyright (C) 2026 EternalChip, Inc.(Gmbh) or its affiliates.
 *
 * All Rights Reserved.
 *
 * @file bsp_key.h
 *
 * @par dependencies
 * - stdio.h
 * - stdint.h
 * - cmsis_os.h
 * - main.h
 * - queue.h
 * - stm32f4xx_hal.h
 * - stm32f4xx_hal_gpio.h
 *
 * @author TNSH Embedded Lab
 *
 * @brief Provide the HAL APIs of Key and corresponding opetions.
 *
 * Processing flow:
 *
 * call directly.
 *
 * @version V1.0 2026-03-15
 *
 * @note 1 tab == 4 spaces!
 *
 *****************************************************************************/

#ifndef __BSP_KEY_H__
#define __BSP_KEY_H__

//******************************** Includes *********************************//

#include <stdint.h> //  the compiling lib.
#include <stdio.h>

#include "cmsis_os.h"
#include "main.h" //  Core / OS layer

#include "queue.h" //  specific file for operations
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_gpio.h"

//******************************** Includes *********************************//

//*********************Thread_Func **********************//
extern osThreadId_t key_TaskHandle;
extern const osThreadAttr_t key_Task_attributes;
//*********************Thread_Func **********************//

//*********************Queue_Handler ********************//
extern QueueHandle_t key_queue;

//*********************Queue_Handler ********************//

//******************************** Defines **********************************//

//按键引脚定义
#define KEY1_PORT GPIOA

#define KEY1_PIN GPIO_PIN_0

//按键数量
#define KEY_NUM 1

//按键事件时间阈值（单位：ms）
#define CONFIRM_TIME_MS 10        /* 消抖时间：10ms */
#define LONG_PRESS_TIME_MS 1000   /* 长按时间：1000ms */
#define DOUBLE_PRESS_TIME_MS 500  /* 双击间隔时间：500ms */

//按键事件有效性配置
#define SHORT_RELEASE_VALID 1  
#define LONG_RELEASE_VALID 1   
#define DOUBLE_RELEASE_VALID 1 

/**
 * @brief 按键操作状态枚举
 * 
 * 定义按键相关操作的返回状态
 */
typedef enum {
  KEY_OK = 0,               /* 操作成功完成 */
  KEY_ERROR = 1,            /* 运行时错误，无匹配情况 */
  KEY_ERRORTIMEOUT = 2,     /* 操作超时失败 */
  KEY_ERRORRESOURCE = 3,    /* 资源不可用 */
  KEY_ERRORPARAMETER = 4,   /* 参数错误 */
  KEY_ERRORNOMEMORY = 5,    /* 内存不足 */
  KEY_ERRORISR = 6,         /* 不允许在中断服务程序中调用 */
  KEY_RESERVED = 0x7FFFFFFF /* 保留值 */
} key_status_t;

/**
 * @brief 按键按压状态枚举
 * 
 * 定义按键状态机的各个状态
 */
typedef enum {
  KEY_NOT_PRESSED = 0,    /* 按键未按下状态 */
  KEY_CONFIRM = 1,        /* 消抖状态 */
  KEY_SHORT_PRESSED = 2,  /* 短按状态 */
  KEY_LONG_PRESSED = 3,   /* 长按状态 */
  KEY_DOUBLE_PRESSED = 4, /* 双击状态 */
} key_press_status_t;

/**
 * @brief 按键事件枚举
 * 
 * 定义按键扫描后产生的事件类型
 */
typedef enum {
  EVENT_NULL,           /* 无事件 */
  EVENT_SHORT_PRESSED,  /* 短按事件 */
  EVENT_DOUBLE_PRESSED, /* 双击事件 */
  EVENT_LONG_PRESSED,   /* 长按事件 */
} key_event_t;

/**
 * @brief 按键GPIO配置结构体
 * 
 * 定义按键对应的GPIO引脚配置
 */
typedef struct {
  GPIO_TypeDef *port;     /* GPIO端口 */
  uint16_t pin;           /* GPIO引脚号 */
  uint16_t pressed_state; /* 按键按下时的电平状态 */
} key_gpio_t;

/**
 * @brief 按键参数结构体
 * 
 * 包含按键状态机运行所需的所有参数
 * 使用HAL_GetTick()获取系统时钟，实现精确的时间测量
 */
typedef struct {
  key_press_status_t current_state; /* 当前按键状态 */
  key_event_t current_event;        /* 当前按键事件 */
  key_gpio_t key_gpio;              /* 按键GPIO配置 */
  uint32_t press_start_tick;        /* 按键按下开始的时间戳（单位：ms） */
  uint32_t last_release_tick;       /* 上次按键释放的时间戳（单位：ms） */
  uint8_t pressed_num;              /* 按键按下次数（用于双击检测） */
} key_param_t;

//******************************** Defines **********************************//

//******************************** Declaring ********************************//

/**
 * @brief 读取按键当前状态
 * 
 * 读取指定按键的GPIO引脚电平，判断按键是否按下
 * 
 * @param[in] key 指向按键参数结构体的指针
 * @return key_status_t 按键状态（KEY_OK表示按下，KEY_ERRORTIMEOUT表示未按下）
 */
key_status_t read_key_state(key_param_t *key);

/**
 * @brief 按键扫描状态机
 * 
 * 实现按键状态机，检测短按、长按、双击等事件
 * 需要周期性调用（建议10ms调用一次）
 * 
 * @param[in] key 指向按键参数结构体的指针
 * @return key_event_t 检测到的按键事件
 */
key_event_t key_scan(key_param_t *key);

/**
 * @brief 按键任务函数
 * 
 * FreeRTOS任务函数，负责按键事件的处理和队列管理
 * 
 * @param[in] argument 任务参数（通常为NULL）
 * @return void
 */
void key_task_func(void *argument);

//******************************** Declaring********************************//

#endif // End of __BSP_KEY_H__

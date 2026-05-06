/******************************************************************************
 * Copyright (C) 2026 EternalChip, Inc.(Gmbh) or its affiliates.
 *
 * All Rights Reserved.
 *
 * @file bsp_key.c
 *
 * @par dependencies
 * - bsp_key.h
 * - stdio.h
 * - stdint.h
 * - cmsis_os.h
 * - main.h
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

#include "bsp_key.h"

osThreadId_t key_TaskHandle;

QueueHandle_t key_queue;

const osThreadAttr_t key_Task_attributes = {
    .name = "key_Task",
    .stack_size = 128 * 4,
    .priority = (osPriority_t)osPriorityHigh,
};



/**
 * @brief 读取按键当前状态
 * 
 * 读取指定按键的GPIO引脚电平，判断按键是否按下。
 * 根据按键配置的按下状态（高电平有效或低电平有效）进行判断。
 * 
 * @param[in] key 指向按键参数结构体的指针
 * @return key_status_t 
 *   - KEY_OK: 按键按下
 *   - KEY_ERRORTIMEOUT: 按键未按下
 */
key_status_t read_key_state(key_param_t *key) {

  if (HAL_GPIO_ReadPin(key->key_gpio.port, key->key_gpio.pin) ==
      key->key_gpio.pressed_state) {
    return KEY_OK; 
  }
  return KEY_ERRORTIMEOUT; 
}


/**
 * @brief 按键扫描状态机
 * 
 * 实现完整的按键状态机，检测短按、长按、双击等事件。
 * 状态机包含5个状态：
 * 1. KEY_NOT_PRESSED: 初始状态，等待按键按下
 * 2. KEY_CONFIRM:     消抖确认状态，确认按键真实按下
 * 3. KEY_SHORT_PRESSED: 短按状态，可转为长按或释放
 * 4. KEY_LONG_PRESSED:  长按状态
 * 5. KEY_DOUBLE_PRESSED: 双击状态
 * 
 * @param[in] key 指向按键参数结构体的指针
 * @return key_event_t 检测到的按键事件
 *   - EVENT_NULL: 无事件
 *   - EVENT_SHORT_PRESSED: 短按事件
 *   - EVENT_LONG_PRESSED: 长按事件
 *   - EVENT_DOUBLE_PRESSED: 双击事件
 * 
 * @note 此函数需要周期性调用，建议调用间隔为KEY_SCAN_INTERVAL（10ms）
 */
key_event_t key_scan(key_param_t *key) {

  /* 局部变量声明 */
  key_status_t ret_key_status = KEY_OK;          /* 按键读取状态 */
  uint32_t current_tick = HAL_GetTick();         /* 当前系统时间戳 */
  /* 局部变量声明结束 */

  /* 读取按键当前物理状态 */
  ret_key_status = read_key_state(key); 

  /* 双击检测：如果已经按下过一次，检查是否超过双击间隔时间 */
  if (key->pressed_num >= 1) 
  {
    /* 计算从上次释放到现在的时间间隔 */
    uint32_t interval = HAL_GetTick() - key->last_release_tick;
    /* 超过双击间隔时间，重置双击计数 */
    if (interval > DOUBLE_PRESS_TIME_MS) {
      key->last_release_tick = 0;
      key->pressed_num = 0;
    }
  }

  /* 状态机主循环 */
  switch (key->current_state) {         
                                
  case KEY_NOT_PRESSED: {               /* 状态1：按键未按下 */
    if (KEY_OK == ret_key_status) {     
      key->current_state = KEY_CONFIRM; /* 进入消抖确认状态 */
      key->press_start_tick = current_tick; /* 记录按下开始时间 */
    } else {                          
      key->current_event = EVENT_NULL;	/* 清除事件 */
    }
    break;
  }
    
  case KEY_CONFIRM: {                             /* 状态2：按键确认（消抖） */
    if (KEY_OK == ret_key_status) {           
      /* 计算按下持续时间 */
      uint32_t press_duration = current_tick - key->press_start_tick;
      if (press_duration > CONFIRM_TIME_MS)     /* 超过消抖时间 */
      {                                       
        key->current_state = KEY_SHORT_PRESSED;   /* 进入短按状态 */
#if (SHORT_RELEASE_VALID == 0)                   /* 如果配置为按下时触发短按事件 */
        key->current_event = EVENT_SHORT_PRESSED; /* 产生短按事件 */
#endif
      }
    } else { 
      key->current_state = KEY_NOT_PRESSED;    /* 按键释放，回到未按下状态 */
    }
    break;
  }
    
  case KEY_SHORT_PRESSED: {                      /* 状态3：短按状态 */
    if (KEY_OK == ret_key_status) {
      /* 计算按下持续时间 */
      uint32_t press_duration = current_tick - key->press_start_tick;
      if (press_duration > LONG_PRESS_TIME_MS) /* 超过长按时间阈值 */
      {                                          
        key->current_state = KEY_LONG_PRESSED;   /* 进入长按状态 */
#if (LONG_RELEASE_VALID == 0)                   /* 如果配置为按下时触发长按事件 */
        key->current_event = EVENT_LONG_PRESSED; /* 产生长按事件 */
#endif
      }
    } else { 
      if (KEY_ERRORTIMEOUT == ret_key_status) {
        /* 计算按下持续时间 */
        uint32_t press_duration = current_tick - key->press_start_tick;
        /* 判断是否为双击的第一下 */
        if (press_duration <= DOUBLE_PRESS_TIME_MS
			&& key->pressed_num < 1) {
          key->pressed_num++;                    /* 增加按下次数 */
          key->last_release_tick = current_tick; /* 记录释放时间 */
          key->current_state = KEY_NOT_PRESSED;     
#if (SHORT_RELEASE_VALID == 1)                     /* 如果配置为释放时触发短按事件 */
          key->current_event = EVENT_SHORT_PRESSED; /* 产生短按事件 */
#endif
        } else if (key->pressed_num >= 1) {     
          key->current_state = KEY_DOUBLE_PRESSED;   /* 进入双击状态 */
#if (DOUBLE_RELEASE_VALID == 0)                      /* 如果配置为按下时触发双击事件 */
          key->current_event = EVENT_DOUBLE_PRESSED; /* 产生双击事件 */
#endif
        }
      }
      break;
    }
  }
    
  case KEY_DOUBLE_PRESSED: {                  /* 状态4：双击状态 */
    if (KEY_ERRORTIMEOUT == ret_key_status) { 
      key->pressed_num = 0;                   
      key->current_state = KEY_NOT_PRESSED;   
#if (DOUBLE_RELEASE_VALID == 1)                  /* 如果配置为释放时触发双击事件 */
      key->current_event = EVENT_DOUBLE_PRESSED; /* 产生双击事件 */
#endif
    }
    break;
  }
    
  case KEY_LONG_PRESSED: {                    /* 状态5：长按状态 */
    if (KEY_ERRORTIMEOUT == ret_key_status) { 
      key->current_state = KEY_NOT_PRESSED;   
#if (LONG_RELEASE_VALID == 1)                  /* 如果配置为释放时触发长按事件 */
      key->current_event = EVENT_LONG_PRESSED; /* 产生长按事件 */
#endif
    }
    break;
  }
    
  default: {                                  /* 默认状态处理     */
    key->current_state = KEY_NOT_PRESSED;     /* 重置为未按下状态 */
	  key->current_event = EVENT_NULL;          /* 清除事件 */
  }
    
  }
  return key->current_event;                  /* 返回当前检测到的事件 */
}


/**
 * @brief 按键任务函数
 * 
 * FreeRTOS任务函数，负责按键事件的处理和队列管理。
 * 主要功能：
 * 1. 创建按键事件队列
 * 2. 等待并处理按键事件
 * 3. 将按键事件发送到其他任务
 * 
 * @param[in] argument 任务参数指针（通常为NULL）
 * @return void
 * 
 * @note 此函数为无限循环，不应返回
 */
void key_task_func(void *argument) {

  /* 创建按键事件队列，容量为10个事件，每个事件大小为uint32_t */
  key_queue = xQueueCreate(10, sizeof(uint32_t));
  if (NULL == key_queue) {
    printf("key_queue created failed \r\n");
  } else {
    printf("key_queue created successfully \r\n");
  }
  
  /* 任务主循环 */
  for (;;) {
  
    osDelay(1); 
  }
}

# 1. **文档概述**

## 1.1 **文档目的**

本文档定义 EC-S100 智能手表项目中软件总体架构、模块分层、任务划分、接口规范和关键机制（功耗、存储、OTA、安全、容错）。

特别关注：

* STM32F411 主控 MCU 侧的系统架构
* BSP 驱动到 APP 的解耦方案
* FreeRTOS 任务调度与数据流
* 与 LVGL UI 的交互模式
* 与 nRF52840 的协同（蓝牙、外设掉电控制）

目标：为团队开发、维护、移植、联调、量产测试、后续 OTA 升级提供统一技术规范。

本文主要描述智能手表双 MCU 中的 STM32F411CEU6 主控，双 MCU 架构图如下文描述所示：

[STM32+Nrf 52840系统架构图](https://twd6onxsxva.feishu.cn/docx/N618dvtN9oqjJnxfDRmcwiGNndh?from=from_copylink)

![[file-20260726213034526.png]]

## 1.2 **适用范围**

硬件：STM32F411CEU6 主控 + nRF52840 辅控 + 外围传感器 (AHT21/MPU6050/气压计等) + W25Q64 外部 Flash + LCD 显示屏 + 触摸屏/按键 + 喇叭等。

系统：FreeRTOS + LVGL

适用读者：

* 手表软件架构开发/维护成员
* BSP/中间件编写人员
* 功耗优化、量产测试、问题定位相关同事

## 1.3 **参考文档**

[00 【STM32+Nordic方案】受限网络工况下的生命体征监控手环\_项目需求分析](https://twd6onxsxva.feishu.cn/docx/Zn44dtKRzoFuchxXH55cPCoMn4g)

[智能手表系统需求规格说明书（SRS）](https://twd6onxsxva.feishu.cn/docx/Clu8dkdjwoODIexxxMXcpX04n4e?from=from_copylink)

[01\_智能手表器件选型](https://twd6onxsxva.feishu.cn/docx/P0DOd6z23oL7ZyxqSeZcrK92njb)

[STM32+Nrf 52840系统架构图](https://twd6onxsxva.feishu.cn/docx/N618dvtN9oqjJnxfDRmcwiGNndh?from=from_copylink)

[资源分配表](https://twd6onxsxva.feishu.cn/sheets/Ny12s6Cj0hdJLgtmjFacBvfendf?sheet=G8euYz)

# 2. **系统架构设计**

## 2.1 **整体架构**

APP 应用层（业务逻辑、UI、场景逻辑）

平台化平台（Platform，支撑 APP 运行的通用基础能力）

Platform 再细分为四个方向：

* MCU Platform：与芯片资源绑定的底座
* OS Platform：调度、同步、定时、任务框架
* BSP Platform：硬件驱动和抽象
* Middleware Platform：算法、通信协议栈、图形库、加密、存储管理等

等价地，我们也可以把整套看成典型多层：

APP 层 → Middleware 层 → BSP 层 → MCU/OS 平台层 → HAL/驱动 → 硬件

这种分层的核心目标是：

* 高内聚：每个模块自己管理自己的资源、状态、接口
* 低耦合：模块间通过接口/消息/回调交互，而不是直接访问彼此内部实现
* 可移植：把“跟具体芯片/具体 RTOS 绑定的部分”集中到适配层（OSAL、HAL 接口指针等），从而允许后续移植到别的 MCU/OS

### 2.1.1 APP 应用层

APP 层负责完整的手表业务逻辑与人机交互，包括：

* UI 页面（表盘、健康、运动、设置、告警、OTA 提示等）
* 传感器场景逻辑（例如心率测量流程、运动计步/抬腕点亮屏、天气界面温湿度刷新）
* 用户交互逻辑（触摸/按键事件、菜单跳转）
* OTA 触发与用户确认流程
* 告警提示（蜂鸣/喇叭/屏幕提示）
* 低电量提示与降能策略触发（UI 层也需要知道电量）

APP 层本身不直接操作底层外设，而是：

* 通过 Handler 层（在 BSP 平台里定义的“业务导向驱动适配层”）去请求数据
* 通过消息队列 / 事件通知获取数据更新
* 通过回调机制获得异步结果（传感器采样完毕回调 UI 刷新）

APP 是基于消息、状态机和回调的高层逻辑。

![[file-20260726213001259.png]]

### 2.1.2 平台化平台

整个架构围绕 APP 应用层为核心，向外分层辐射，形成一个可模块化、可扩展、低耦合的多层平台体系。

四大方向分别对应系统运行的核心支撑面：MCU Platform、OS Platform、BSP Platform、Middleware Platform、Service Platform。

平台化平台是 APP 的运行地基，由 MCU Platform / OS Platform / BSP Platform / Middleware Platform 四个支撑面协同组成。

关键点：

* MCU Platform：封装芯片时钟、Flash 映射、低功耗入口/退出流程、Bootloader 跳转、Watchdog、异常捕获（cmbacktrace）等。
* OS Platform：FreeRTOS 抽象（OSAL），统一任务创建、队列、互斥锁、事件组、Notify 等 IPC。让上层逻辑不直接依赖 FreeRTOS 原 API，利于后续移植或替换 RTOS。
* BSP Platform：所有外设驱动、传感器驱动、显示/触摸/喇叭等 I/O 控制封装成“可复用、可热插拔”的组件。BSP 进一步分为 Driver 层和 Handler 层（见下）。
* Middleware Platform：图形库 (LVGL)、算法（计步/抬腕检测等）、安全模块（AES、哈希）、协议栈（Ymodem OTA、BLE 透传适配）、存储服务（配置管理、日志、分区表管理）等。

这些支撑面由单元测试和集成测试保证质量，APP 只是使用它们，不感知内部细节。

![[file-20260726211535857.png]] ![]()

### 2.1.3 BSP 驱动层

BSP 层进一步拆成两部分：

* Driver 层（硬件抽象层 HAL/Driver）：直接和具体外设打交道（I2C/SPI/UART/ADC/DMA/GPIO/PWM 等），负责寄存器级或 HAL 级操作；
* Handler 层（业务侧驱动管理器）：用桥接模式/适配器模式，把 Driver 的细节封装成高层接口，提供：
  * 初始化/反初始化
  * 线程化的采集流程
  * 异步回调
  * 去抖、滤波、缓冲、数据新鲜度管理
  * 多调用者仲裁
  * 错误恢复/容灾（I2C 故障重试、数据校验、回退到上一次有效值等）

Handler 层的目标是：APP 不关心“具体是 AHT21 还是 DHT11”，APP 只说“我要温度湿度，回调给我”，Handler 自己决定怎么拿数据、怎么调度传感器采集线程、怎么复用资源、怎么做功耗友好采样。

这种分层 + 桥接的好处：

* 高内聚：AHT21 的状态、采样队列、定时器、互斥锁都内聚在它自己的 Handler/Driver 组合里，而不是散落在 APP 或别的模块。
* 低耦合：Handler 对外暴露统一接口，底层 Driver 可以换芯片（AHT21 ⇄ DHT11）但不改 APP。
* 可运行时挂载：接口指针（I2C 访问接口、OS 延时接口、系统时基接口等）在构造阶段注入，而不是编译期死绑芯片和 OS → 移植非常容易。

（这是本项目驱动架构的核心技术卖点之一）

![[file-20260726211549256.png]]

### 2.1.4 MCU 层

MCU Platform 主要职责：

* 时钟树初始化与反初始化（包括 Bootloader→APP 跳转时的“逆初始化再正初始化”策略，避免 PLL 寄存器写入失败）
* 片上 Flash/外部 W25Q64 Flash 分区管理（Bootloader 分区、APP 分区、OTA 缓存分区、日志区等）
* 上电自检：固件哈希校验、升级标志位检查、回滚逻辑
* 低功耗模式入口/恢复：STOP 模式、Tickless 模式、外设掉电/上电时序，和 nRF52840 的电源域协作
* Watchdog（硬件独立看门狗 + 任务级看门狗线程配合）
* 异常捕获：HardFault/MemManage/BusFault/UsageFault 记录栈和寄存器信息到日志区（cmbacktrace），为现场诊断和 DevOps 闭环提供数据
* 安全基础：AES-128 解密、哈希校验（CRC32 或更高强度哈希）、数字签名验证等作为可信启动链条的一部分，为 OTA 后的固件可信度兜底

### 2.1.5 Middleware 层

Middleware Platform 站在 APP 和 BSP 之间，作用是“共性逻辑复用 + 算法 + 协议 + 服务”：

典型组件：

* LVGL 图形库适配层
  * 屏幕驱动 flush 回调
  * 触摸事件注入
  * UI 刷新节流与功耗优化（比如暗屏后不再高频刷新）
* 运动算法模块
  * MPU6050 数据消费（通过 DMA+ 中断 + 双 buffer 上传的加速度数据）
  * 步数统计 / 抬腕判定 / 跌倒检测
  * 滑动平均滤波 / 窗口滤波 / 合加速度计算
* OTA 协议模块
  * BLE(由 nRF 侧) → Ymodem → STM32
  * 固件分片接收、CRC 校验、加密缓存到 W25Q64 的 A 区
  * 设置升级 Flag（EEPROM/配置区）等待重启后 Bootloader 执行搬运和回滚保护
* 存储服务模块
  * 配置项读写（config\_load/save）
  * Flash 区块抽象（读/写/擦），提供统一接口给上层
  * 日志服务（错误日志、watchdog 异常记录、系统状态记录）
* 安全/加密
  * AES 固件解密
  * 固件完整性校验
  * 后续可扩展数字签名验证机制
* 电源策略服务
  * 根据系统空闲时长/屏幕熄灭状态/运动强度，动态下发功耗模式切换指令给 MCU Platform 和 BSP Handler （例如降采样、停采样、关外设电源）

这些模块都不直接面对 UI，而是为 APP 层和 Handler 层提供可复用的“服务能力”。

### 2.1.6 OS 层

OS Platform 对 FreeRTOS 做二层封装（OSAL）：

* 任务创建/销毁
* 消息队列
* 事件组
* 互斥锁/信号量
* 任务通知 (Notify) 及邮箱式四字节快速消息
* 定时器
* 任务优先级管理
* Tickless 与 Idle 钩子支持
* 看门狗喂养/检测机制

我们把这些封装成统一接口，避免 APP/Handler 直接依赖 FreeRTOS 原生 API，方便未来迁移到其他 RTOS 或裸机 + 调度框架。

另外，核心调度和关键中断服务函数通过链接脚本（SCT 分散加载）放到 SRAM 中，缩短中断延时和上下文切换开销，提升系统实时性与功耗表现。

### 2.1.7 Service 层

Service Platform 对 APP 中可以抽象出来服用的逻辑进行抽象：

## 2.2 **数据流向设计**

### 2.2.1 **按需采样策略**

* 温湿度 (AHT21)：仅在相关界面（天气/环境信息界面）激活时采样，约 0.1Hz
* 心率：仅在健康监测界面或用户主动测量时激活，约 1Hz
* 运动 (MPU6050)：在运动/计步相关场景激活，10Hz 甚至更高的内部采样频率（实际内部采集>>上报频率）
* 电池：全局低频检测，0.1Hz 级别，滤波计算后上报 UI
* 触摸/按键：高频实时响应（触摸 100Hz 采样/中断触发）
* 显示刷新：按需刷新，>=60Hz 时只在亮屏交互态保持高刷新，息屏或低功耗状态下降低刷新率乃至关背光

### 2.2.2 **数据交互机制选择**

**高频数据（>60Hz）**：

* 触摸数据：中断 + 直接函数回调到 LVGL 输入驱动
* 显示刷新：DMA + 回调完成通知（LVGL flush\_cb）

**中频数据（1-10Hz）**：

* 运动/步数/手势：Handler 线程通过消息队列推送给系统事件队列，UI 订阅系统事件
* 心率：定时采样线程→消息队列→系统事件

**低频数据（<1Hz）**：

\- 温湿度：Handler 内部缓存最新值，并用事件通知 UI 刷新

\- 电池/电量：周期性更新全局 system\_status，再抛系统事件

\- 静态/配置数据：直接函数调用（storage\_interface\_t）

***

高频 = 实时交互，要求延迟极低 → 中断驱动/直接回调。

中频 = 行为反馈类数据 → 队列/事件组。

低频 = 趋势类数据 → 全局状态 + 懒刷新。

# 3. APP 应用层架构

## 3.1 Ui 设计

### 3.1.1 界面设计图

#### 3.1.1.1 主界面（001）

![[file-20260726211615580.png]]

#### 3.1.1.2 主界面（002）

![[file-20260726211643790.png]]

#### 3.1.1.3 表盘界面（003）

![[file-20260726211657664.png]]

#### 3.1.1.4 外部设置界面（004）

![[file-20260726211715900.png]]

#### 3.1.1.5 通用设置界面（005）

![[file-20260726211733005.png]]

#### 3.1.1.6 默认菜单（006）

![[file-20260726212841089.png]]

#### 3.1.1.7 环状菜单（007）

![[file-20260726212857466.png]]

#### 3.1.1.8 传感器数据显示界面（008）

![[file-20260726212913058.png]]

#### 3.1.1.9 NFC 界面（009）

![[file-20260726212930410.png]]

#### 3.1.1.10 心率界面（010）

![]()

#### 3.1.1.11 二维码界面（011）

![]()

#### 3.1.1.12 节点异常丢失界面（012）

![]()

#### 3.1.1.13 电子围栏界面（013）

![]()

#### 3.1.1.14 App 更新请求界面（014）

![]()

#### 3.1.1.15 App 版本检测界面（015）

![]()

#### 3.1.1.16 升级界面（016，017）

![]()

#### 3.1.1.17 App 请求安装界面（018）

![]()

#### 3.1.1.18 菜单设置界面（019）

![]()

### 3.1.2 Ui 界面切换逻辑

![[file-20260726211819722.png]]

## 3.2 **数据交互接口设计**

### 3.2.1 **数据结构定义**

```c
/* 系统事件类型 */
typedef enum {
    SYS_EVENT_NONE = 0,
    SYS_EVENT_SENSOR_UPDATE,
    SYS_EVENT_BATTERY_UPDATE,
    SYS_EVENT_BT_STATUS_CHANGE,
    SYS_EVENT_UI_SWITCH,
    SYS_EVENT_POWER_MODE_CHANGE
} system_event_type_t;

/* 界面状态枚举 */
typedef enum {
    UI_STATE_WATCHFACE = 0,
    UI_STATE_HEALTH,
    UI_STATE_SPORT,
    UI_STATE_SETTINGS,
    UI_STATE_WEATHER,
    UI_STATE_SLEEP
} ui_state_t;

/* 传感器数据请求结构 */
typedef struct {
    uint32_t sensor_mask;     /* 请求的传感器位掩码 */
    uint32_t sample_rate;     /* 采样频率 */
    uint32_t duration;        /* 采样持续时间 */
} sensor_request_t;

/* 系统状态全局变量 */
typedef struct {
    ui_state_t current_ui_state;
    uint8_t battery_level;
    uint8_t bt_status;
    uint8_t power_mode;
    uint32_t system_tick;
} system_status_t;
```

### 3.2.2 **消息队列设计**

```c
/* 传感器数据消息队列 */
#define SENSOR_QUEUE_SIZE       10
#define SENSOR_MSG_SIZE         sizeof(st_sensor_data_t)

/* 系统事件消息队列 */
#define EVENT_QUEUE_SIZE        20
#define EVENT_MSG_SIZE          sizeof(system_event_type_t)

/* UI事件消息队列 */
#define UI_EVENT_QUEUE_SIZE     15
#define UI_EVENT_MSG_SIZE       sizeof(ui_state_t)
```

### 3.2.3 **接口函数规范**

#### 3.2.3.1 **传感器接口**

```c
/* 传感器管理接口 */
typedef struct {
    /* 传感器控制接口 */
    int32_t (*sensor_start)(uint32_t sensor_mask, uint32_t sample_rate);
    int32_t (*sensor_stop)(uint32_t sensor_mask);
    int32_t (*sensor_get_data)(uint32_t sensor_type, void* data, uint32_t timeout);
    
    /* 传感器状态查询 */
    bool (*sensor_is_active)(uint32_t sensor_type);
    uint32_t (*sensor_get_sample_rate)(uint32_t sensor_type);
} sensor_interface_t;

/* 具体传感器接口实现 */
float sensor_temp_read(void);                    /* 温度读取 */
float sensor_humidity_read(void);                /* 湿度读取 */
uint16_t sensor_heartrate_read(void);            /* 心率读取 */
uint32_t motion_get_steps(void);                 /* 步数获取 */
void motion_get_accel(float *x, float *y, float *z);  /* 加速度获取 */
void motion_get_gyro(float *x, float *y, float *z);   /* 角速度获取 */
```

#### 3.2.3.2 **显示接口**

```c
/* 显示管理接口 */
typedef struct {
    bool (*display_init)(uint16_t width, uint16_t height, uint8_t color_depth);
    void (*display_flush)(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const void* color_map);
    void (*display_set_backlight)(uint16_t brightness);
    bool (*display_is_busy)(void);
} display_interface_t;
```

#### 3.2.3.3 **存储接口**

```c
/* 存储管理接口 */
typedef struct {
    bool (*config_load)(void* config_struct, uint32_t size);
    bool (*config_save)(const void* config_struct, uint32_t size);
    bool (*flash_read)(uint32_t addr, void* data, uint32_t len);
    bool (*flash_write)(uint32_t addr, const void* data, uint32_t len);
    bool (*flash_erase)(uint32_t addr, uint32_t len);
} storage_interface_t;
```

## 3.3 **任务设计与调度**

### 3.3.1 **任务优先级分配**

参考 [资源分配表](https://twd6onxsxva.feishu.cn/sheets/Ny12s6Cj0hdJLgtmjFacBvfendf?from=from_copylink) 1-Task 任务分配表格

### 3.3.2 **任务间通信设计**

```c
/* 全局队列句柄 */
extern osal_queue_handle_t g_sensor_data_queue;
extern osal_queue_handle_t g_system_event_queue;
extern osal_queue_handle_t g_ui_event_queue;

/* 全局互斥锁 */
extern osal_mutex_handle_t g_display_mutex;
extern osal_mutex_handle_t g_storage_mutex;
extern osal_mutex_handle_t g_sensor_mutex;

/* 全局事件组 */
extern osal_event_group_handle_t g_system_events;

/* 事件位定义 */
#define EVENT_SENSOR_READY      (1 << 0)
#define EVENT_DISPLAY_READY     (1 << 1)
#define EVENT_STORAGE_READY     (1 << 2)
#define EVENT_UI_SWITCH         (1 << 3)
#define EVENT_LOW_BATTERY       (1 << 4)
```

## 3.4 **具体模块交互逻辑**

### 3.4.1 **温湿度传感器交互逻辑**

进入天气界面 → 通过队列发送 sensor\_request\_t → Handler 启动 AHT21 采样线程 → 新数据回调 UI 标签刷新 → 退出界面时发送 stop 请求并注销回调。

亮点：

* AHT21 使用软件 I2C，因为其时序不完全标准；
* Handler 提供“数据新鲜度”概念（比如 300ms 内的同一温度值可以复用，而不必重复 I2C 读），避免高频重复采集，降功耗；
* 读取是异步的：APP 只注册回调，Handler 完成后回调通知，APP 线程不阻塞。

#### 3.4.1.1 接口传递流程图

![[file-20260726211857669.png]]

#### 3.4.1.2 相关代码

```c
/* UI切换到天气界面时的处理流程 */
void ui_weather_screen_enter(void) {
    /* 1. 发送传感器启动请求 */
    sensor_request_t req = {
        .sensor_mask = SENSOR_TEMP | SENSOR_HUMIDITY,
        .sample_rate = 100,  /* 0.1Hz = 100 * 10ms */
        .duration = 0        /* 持续采样直到界面退出 */
    };
    
    /* 2. 通过队列发送请求 */
    os_queue_send(g_sensor_request_queue, &req, 100);
    
    /* 3. 注册数据更新回调 */
    sensor_register_callback(SENSOR_TEMP, temp_data_callback);
    sensor_register_callback(SENSOR_HUMIDITY, humidity_data_callback);
}

/* 温度数据回调函数 */
void temp_data_callback(float temperature) {
    /* 更新全局变量 */
    g_system_status.temperature = temperature;
    
    /* 通知LVGL更新界面 */
    lv_event_send(weather_temp_label, LV_EVENT_REFRESH, NULL);
}

/* UI退出天气界面时的处理 */
void ui_weather_screen_exit(void) {
    /* 停止传感器采样 */
    sensor_request_t req = {
        .sensor_mask = SENSOR_TEMP | SENSOR_HUMIDITY,
        .sample_rate = 0,    /* 停止采样 */
        .duration = 0
    };
    os_queue_send(g_sensor_request_queue, &req, 100);
    
    /* 注销回调函数 */
    sensor_unregister_callback(SENSOR_TEMP);
    sensor_unregister_callback(SENSOR_HUMIDITY);
}
```

### 3.4.2 **心率传感器交互逻辑**

心率测量状态机：

* HR\_STATE\_IDLE → HR\_STATE\_MEASURING → HR\_STATE\_STABLE → HR\_STATE\_ERROR
* 启动时下发 sensor\_request\_t，1Hz 采样，30 秒测量周期
* 心率数据做滑动平均滤波（10 点平均）后，如果范围合理 (40\~200 bpm) 则写入 g\_system\_status.heartrate 并发系统事件 SYS\_EVENT\_HEARTRATE\_UPDATE
* UI 订阅该事件更新显示

亮点：

* 数据校验 + 滤波，确保 UI 显示的是“稳定值”而不是瞬时噪声
* 超时定时器防止无休止测量，影响功耗
* 状态机让 UI 可以给出“正在测量… / 稳定值 / 测量失败”反馈

#### 3.4.2.1 接口传递流程图

![[file-20260726211918287.png]]

#### 3.4.2.2 相关代码

```c
/* 心率监测的状态机设计 */
typedef enum {
    HR_STATE_IDLE,
    HR_STATE_MEASURING,
    HR_STATE_STABLE,
    HR_STATE_ERROR
} heartrate_state_t;

/* 心率测量控制 */
void heartrate_measurement_start(void) {
    /* 1. 设置高频采样 */
    sensor_request_t req = {
        .sensor_mask = SENSOR_HEARTRATE,
        .sample_rate = 1000,  /* 1Hz */
        .duration = 30000     /* 30秒测量周期 */
    };
    
    /* 2. 启动测量任务 */
    os_queue_send(g_sensor_request_queue, &req, 100);
    
    /* 3. 设置状态 */
    g_heartrate_state = HR_STATE_MEASURING;
    
    /* 4. 启动超时定时器 */
    os_timer_start(g_heartrate_timeout_timer, 30000);
}

/* 心率数据处理 */
void heartrate_data_process(uint16_t raw_data) {
    static uint16_t hr_buffer[10];
    static uint8_t buffer_index = 0;
    
    /* 滑动平均滤波 */
    hr_buffer[buffer_index] = raw_data;
    buffer_index = (buffer_index + 1) % 10;
    
    /* 计算平均值 */
    uint32_t sum = 0;
    for(int i = 0; i < 10; i++) {
        sum += hr_buffer[i];
    }
    uint16_t avg_hr = sum / 10;
    
    /* 数据有效性检查 */
    if(avg_hr >= 40 && avg_hr <= 200) {
        g_system_status.heartrate = avg_hr;
        g_heartrate_state = HR_STATE_STABLE;
        
        /* 通知UI更新 */
        system_event_type_t event = SYS_EVENT_HEARTRATE_UPDATE;
        os_queue_send(g_system_event_queue, &event, 0);
    }
}
```

### 3.4.3 **气压传感器交互逻辑**

同理，可复用 AHT21 的模式：

* 进入相关页面/功能时启动
* 以中低频率采集
* 结果通过事件队列投递给 UI

（后续在定稿中可加具体流程图与接口）

#### 3.4.3.1 接口传递流程图

![[file-20260726211944960.png]]

#### 3.4.3.2 相关代码

### 3.4.4 **运动传感器交互逻辑**

MPU6050 使用硬件 I2C+DMA+ 中断 + 双缓冲的高性能路径：

* FIFO 满 (1024 bytes) 触发 INT，引脚中断
* 中断服务例程极短：切换 DMA 目标 buffer、发任务通知给 MPU6050 线程
* MPU6050 线程在被唤醒后批量处理 170 组左右的加速度样本：
  * 均值滤波
  * 计算合加速度
  * 步数计数（防抖、300ms 间隔）
  * 抬腕识别（判断是否要亮屏）
* 每 10 步发送 SYS\_EVENT\_STEP\_UPDATE 事件，不必每步都打扰 UI
* 在低功耗状态下，MPU6050 可被配置成运动唤醒源：
  * 当 MCU 处于 STOP 模式时，仅 MPU6050 工作在运动检测中断模式
  * 检测到特定运动阈值中断 → 唤醒 MCU → MCU 切到 FIFO 模式高采样，分析姿态是否是“抬腕点亮屏”

亮点（卖点）：

* 中断“快进快出”：实质工作在线程里完成，避免中断里长时间搬数据
* DMA+ 双 buffer：避免丢样，避免中断嵌套导致的 buffer 覆盖
* 低功耗一体化：MPU6050 既是数据源，也是唤醒源，是整机功耗策略的一环

#### 3.4.4.1 接口传递流程图

![[file-20260726212004750.png]]

#### 3.4.4.2 运动控制代码

```c
/* 运动数据管理 */
typedef struct {
    uint32_t daily_steps;
    float total_distance;
    uint32_t calories_burned;
    bool is_walking;
    bool is_running;
} motion_data_t;

/* 运动检测算法 */
void motion_detection_algorithm(st_sensor_motion_data_t* motion_data) {
    static float accel_magnitude_prev = 0;
    static uint32_t step_count = 0;
    static uint32_t last_step_time = 0;
    
    /* 计算加速度幅值 */
    float accel_magnitude = sqrt(motion_data->ax * motion_data->ax + 
                                motion_data->ay * motion_data->ay + 
                                motion_data->az * motion_data->az);
    
    /* 步数检测算法 */
    if(accel_magnitude > 1.2 && accel_magnitude_prev < 1.2) {
        uint32_t current_time = os_task_get_tick_count();
        if(current_time - last_step_time > 300) {  /* 防抖动 */
            step_count++;
            last_step_time = current_time;
            
            /* 更新全局步数 */
            g_motion_data.daily_steps = step_count;
            
            /* 每10步通知一次UI */
            if(step_count % 10 == 0) {
                system_event_type_t event = SYS_EVENT_STEP_UPDATE;
                os_queue_send(g_system_event_queue, &event, 0);
            }
        }
    }
    
    accel_magnitude_prev = accel_magnitude;
}
```

### 3.4.5 **显示系统交互逻辑**

LVGL 调用 display\_flush() → 我们用 SPI+DMA 把像素块刷到 LCD → 完成后回调 lv\_disp\_flush\_ready()。

 背光亮度由 display\_set\_backlight() 控制，可被功耗管理模块动态调低/关断。

UI 线程在高亮屏模式下维持高刷新；进入休眠/灭屏后停止 UI 高频刷新，降低 DMA 占用和总线功耗。

### 3.4.6 **存储系统交互逻辑**

上层通过 storage\_interface\_t 调用：

* config\_save() / config\_load() 保存用户配置、电源管理策略、OTA 升级标志等
* flash\_xxx 读写外部 W25Q64 的分区，支持：
  * OTA 固件 A 区/B 区
  * 日志区（错误栈、看门狗记录）
  * 运动/健康等历史记录（视产品需求）

所有 Flash 操作都集中到一处，便于做：

* AES 解密/加密
* CRC/哈希校验
* 回滚机制（AB 区）
* Flash 磨损管理策略

## 3.5 **电源管理与性能优化**

3.5.1 电源模式管理

&#x20; 系统定义多种功耗模式，例如：

```c++
typedef enum {
    POWER_MODE_NORMAL,      /* 正常运行 */
    POWER_MODE_LOW_POWER,   /* 降频+降采样 */
    POWER_MODE_SLEEP,       /* 屏幕灭，绝大多数组件停转，Tickless */
    POWER_MODE_DEEP_SLEEP   /* STOP模式，靠MPU6050等极少数外设唤醒 */
} power_mode_t;

```

策略示例：

* 空闲 30s：进入 POWER\_MODE\_LOW\_POWER
  * 降低传感器采样率
  * 背光调暗
* 空闲 60s：进入 POWER\_MODE\_SLEEP
  * 背光关
  * LVGL 刷新降到极低或暂停
  * Tickless + 降频
* 更长无交互：进入 STOP 模式（相当于 POWER\_MODE\_DEEP\_SLEEP）
  * STM32 进入 STOP，主时钟关闭，仅保持 SRAM/寄存器
  * nRF52840 通过串口指令关掉大部分传感器供电，仅保留 MPU6050 运动检测
  * MPU6050 运动阈值中断或触摸/按键中断唤醒系统
  * 唤醒后恢复时钟、恢复 LVGL、恢复采样线程

为了平衡体验和功耗，我们采用“渐进式降功耗”而不是立即 STOP，避免频繁进出 STOP 带来初始化开销和 UI 体验抖动。

### 3.5.1 **电源模式管理**

```c
/* 电源模式定义 */
typedef enum {
    POWER_MODE_NORMAL,      /* 正常模式 */
    POWER_MODE_LOW_POWER,   /* 低功耗模式 */
    POWER_MODE_SLEEP,       /* 睡眠模式 */
    POWER_MODE_DEEP_SLEEP   /* 深度睡眠模式 */
} power_mode_t;

/* 电源模式切换策略 */
void power_mode_manager(void) {
    static uint32_t last_activity_time = 0;
    uint32_t current_time = os_task_get_tick_count();
    uint32_t idle_time = current_time - last_activity_time;
    
    /* 根据空闲时间切换电源模式 */
    if(idle_time > 60000) {  /* 1分钟无操作 */
        power_set_mode(POWER_MODE_SLEEP);
        /* 停止非必要传感器 */
        sensor_stop(SENSOR_TEMP | SENSOR_HUMIDITY);
        /* 降低显示刷新率 */
        display_set_refresh_rate(1);
    } else if(idle_time > 30000) {  /* 30秒无操作 */
        power_set_mode(POWER_MODE_LOW_POWER);
        /* 降低传感器采样率 */
        sensor_set_sample_rate(SENSOR_ALL, 50);
    }
}
```

### 3.5.2 **内存管理优化**

传感器数据使用固定内存池（静态数组 + 位图 bitmap 管理），分配和释放都是 O(N) 扫描，不走堆 malloc，避免碎片。

UI 相关大 buffer（图标缓存等）通过链接脚本放入特定 SRAM 段，并用 MPU 进行保护，帮助在调试阶段捕获越界写（MemManage Fault 比 HardFault 更易定位）。

FreeRTOS 关键调度路径、中断向量表、中断服务函数放到 SRAM 以降低中断延迟。

使用栈水位监控 (0xA5A5A5A5 填充) 和 FreeRTOS 栈溢出钩子，防止任务栈不足导致的随机崩溃。

```c
/* 内存池管理 */
#define SENSOR_DATA_POOL_SIZE   10
#define UI_EVENT_POOL_SIZE      20

static st_sensor_data_t sensor_data_pool[SENSOR_DATA_POOL_SIZE];
static uint8_t sensor_pool_bitmap = 0;

/* 内存池分配 */
st_sensor_data_t* sensor_data_alloc(void) {
    os_enter_critical();
    
    for(int i = 0; i < SENSOR_DATA_POOL_SIZE; i++) {
        if(!(sensor_pool_bitmap & (1 << i))) {
            sensor_pool_bitmap |= (1 << i);
            os_exit_critical();
            return &sensor_data_pool[i];
        }
    }
    
    os_exit_critical();
    return NULL;  /* 内存池已满 */
}

/* 内存池释放 */
void sensor_data_free(st_sensor_data_t* ptr) {
    if(ptr >= sensor_data_pool && ptr < sensor_data_pool + SENSOR_DATA_POOL_SIZE) {
        int index = ptr - sensor_data_pool;
        os_enter_critical();
        sensor_pool_bitmap &= ~(1 << index);
        os_exit_critical();
    }
}
```

## 3.6 **错误处理与容错机制**

### 3.6.1 **传感器错误处理**

&#x20; 统一的 sensor\_error\_handler():

* 超时：重新 init 传感器
* I2C 失败：总线复位 + 重新 init
* 数据无效：回退到上次有效值
* 校准失败：记录错误日志，提示 UI 或者延后重试

Handler 负责这些重试和容灾，APP 不需要关心“为什么刚才心率读不到”

```c
/* 传感器错误类型 */
typedef enum {
    SENSOR_ERROR_NONE = 0,
    SENSOR_ERROR_TIMEOUT,
    SENSOR_ERROR_I2C_FAIL,
    SENSOR_ERROR_DATA_INVALID,
    SENSOR_ERROR_CALIBRATION_FAIL
} sensor_error_t;

/* 传感器错误处理 */
void sensor_error_handler(uint32_t sensor_type, sensor_error_t error) {
    switch(error) {
        case SENSOR_ERROR_TIMEOUT:
            /* 重新初始化传感器 */
            sensor_reinit(sensor_type);
            break;
            
        case SENSOR_ERROR_I2C_FAIL:
            /* 重置I2C总线 */
            i2c_bus_reset();
            sensor_reinit(sensor_type);
            break;
            
        case SENSOR_ERROR_DATA_INVALID:
            /* 使用上次有效数据 */
            sensor_use_last_valid_data(sensor_type);
            break;
            
        default:
            /* 记录错误日志 */
            log_error("Sensor error: type=%d, error=%d", sensor_type, error);
            break;
    }
}
```

### 3.6.2 **看门狗与健壮性**

有两层看门狗：

* FreeRTOS 级 “任务看门狗线程”
  * 每个关键任务注册自己 + 喂狗周期
  * 任务看门狗线程定期检查：如果某个任务长时间没喂狗，说明它卡死或优先级被饿死
  * 如果检测到问题，记录日志并尝试局部恢复（重启该模块的 Handler 或触发软复位）
* 硬件独立看门狗（IWDG）
  * 如果系统整体长时间（例如>2s）没有喂狗，IWDG 复位 MCU
  * Bootloader 启动后可以识别“上次是异常复位还是正常复位”，并决定是否回滚固件、提示用户

结合 cmbacktrace，把异常现场（寄存器/栈回溯）落到 Flash 日志区，量产后现场故障也能回收分析，这对后续 OTA 迭代和质量闭环非常关键。

```c
/* 任务看门狗 */
#define WATCHDOG_TIMEOUT_MS     5000

typedef struct {
    const char* task_name;
    uint32_t last_feed_time;
    uint32_t timeout_ms;
    bool is_enabled;
} task_watchdog_t;

static task_watchdog_t task_watchdogs[] = {
    {"LVGL_Task", 0, 1000, true},
    {"Sensor_Task", 0, 2000, true},
    {"Display_Task", 0, 1000, true}
};

/* 看门狗喂狗 */
void watchdog_feed(const char* task_name) {
    for(int i = 0; i < sizeof(task_watchdogs)/sizeof(task_watchdog_t); i++) {
        if(strcmp(task_watchdogs[i].task_name, task_name) == 0) {
            task_watchdogs[i].last_feed_time = os_task_get_tick_count();
            break;
        }
    }
}

/* 看门狗检查任务 */
void watchdog_check_task(void *argument) {
    while(1) {
        uint32_t current_time = os_task_get_tick_count();
        
        for(int i = 0; i < sizeof(task_watchdogs)/sizeof(task_watchdog_t); i++) {
            if(task_watchdogs[i].is_enabled) {
                uint32_t elapsed = current_time - task_watchdogs[i].last_feed_time;
                if(elapsed > task_watchdogs[i].timeout_ms) {
                    /* 任务超时，记录错误并重启系统 */
                    log_error("Task %s timeout, system reset", task_watchdogs[i].task_name);
                    NVIC_SystemReset();
                }
            }
        }
        
        os_task_delay_ms(1000);
    }
}
```

# 4. 平台化平台架构

## 4.1 MCU Platform

### 4.1.1 用户接口层

* 向上暴露与 MCU 相关的通用服务：系统复位、时钟切换、进入/退出低功耗模式、喂硬件看门狗、获取系统 tick 等。
* 上层（APP/Middleware）不直接操作寄存器，而是调用这些接口。

### 4.1.2 管理层

* Bootloader/APP 分区管理
* OTA 升级状态标志（在 EEPROM 或配置区）
* 回滚及双区固件搬运策略（A/B 区 + 校验 + 回退）
* 日志存取（错误栈/硬 fault 信息）

### 4.1.3 内核支持层

* 时钟树/PLL 初始化与反初始化
* 中断向量表重定位（Bootloader→APP）
* SRAM 加速路径的 SCT 分散加载
* MPU 配置（内存保护、捕获数组越界等）

### 4.1.4 测试支持层

* 上电自检：校验固件完整性
* 健康状态导出接口，用于产线测试工具快速验证核心外设（I2C、SPI、DMA、UART）和 Flash 可写性

### 4.1.5 外设驱动层

* 针对 MCU 本身片上外设（I2C、SPI、ADC、UART、TIM/PWM、DMA、GPIO 等）提供统一 HAL 访问接口
* 这些接口作为 Driver 层的基础

#### 外设类图

**IIC:**

![[file-20260726212055719.png]]

**SPI:**

![[file-20260726212112733.png]]

**GPIO:**

![[file-20260726212127494.png]]

**ADC:**

![[file-20260726212141665.png]]

**USART:**

![[file-20260726212156270.png]]

**Overview:**

![[file-20260726212211773.png]]

## 4.2 OS Platform

### 4.2.1 系统管理层

* FreeRTOS 内核配置（抢占/优先级/定时器/内存分配策略）
* Tickless 模式策略
* 任务优先级整体规划和统一表

### 4.2.2 OS 抽象层

* 提供统一的任务/队列/互斥锁/事件组/Notify API
* 任务看门狗登记/喂狗接口
* Idle 钩子统一处理（降频、功耗统计）

### 4.2.3 测试支持层

* SystemView/Trace Hook
* 栈水位检测/任务运行时统计接口
* 覆盖率/活性检查（是否有任务饿死）

### 4.2.4 外设驱动层

* 用于把中断事件安全地转交到线程世界的“中断转发机制”（例如：MPU6050 中断 ISR 里发 Notify 唤醒处理线程）
* OS 定时器封装（心率超时、功耗超时等）

## 4.3 BSP Platform

### 4.3.1 驱动实例层 Adapter

* 具体外设驱动：AHT21、MPU6050、触摸 IC、LCD、Speaker、ADC、电源管理芯片、NFC 模块等
* 这些驱动关注“怎么跟这个芯片说话”(I2C/SPI/UART/PWM/ADC 等)，以及基本的数据拿取

### 4.3.2 抽象接口层 Wrapper

* 高层业务驱动管理器
* 提供异步接口、回调注册、数据新鲜度、缓冲和滤波
* 负责避免资源冲突（多任务同时读同一 I2C 总线）、负责节电（按需采样）、负责错误修复（I2C reset 重试）

### 4.3.3 测试支持层

* 提供标准的自检 API（比如“读一次温度返回值/状态码”）
* 工装夹具可调用这些接口做产线校准（写入校准系数到 Flash）

## 4.4 Middleware Platform

### 4.4.1 交互接口层

* UI 桥接（LVGL）
* BLE 透传/OTA 命令接收接口（来自 nRF52840）
* 与 APP 对接的高层服务接口

### 4.4.2 数据处理层

* 心率滤波、步数统计、手势识别、抬腕亮屏算法
* 传感器校准系数应用
* 温湿度/气压补偿

### 4.4.3 系统支撑层

* 存储抽象（config、固件分区、日志区）
* 日志/错误记录/回放
* 看门狗策略与错误恢复协调

### 4.4.4 安全加密层

* AES 解密固件
* CRC32/Hash 校验
* 数字签名验证（可扩展）
* 可信启动链路

### 4.4.5 算法与显示层

* 抬腕动作判断 → 通知显示模块点亮屏幕
* 低电量 (电池曲线） → 通知 UI 弹窗 or 通知 Speaker 播放提示音

### 4.4.6 测试支持层

* 模拟数据注入（例如用假步数/假心率喂 UI，验证 UI 逻辑和功耗逻辑而不依赖真实传感器）
* 回环测试接口（环回 BLE 下行指令，验证 OTA 命令链路）

# 5. BSP 驱动层程序架构

BSP 驱动层是本项目的“王牌”。

主要设计原理：

* 桥接模式 + 运行时挂载接口|里氏替换
  * Driver 层不直接调用 FreeRTOS、也不直接知道 timebase 或延时怎么实现
  * 这些依赖通过结构体中的函数指针在运行时注入
  * 这样同一套 Handler + Driver 可以移植到不同 MCU、不同 OS、不同 I2C 控制器实现
* Handler 线程化
  * 每个复杂外设（AHT21、MPU6050、Speaker 等）往往对应一个（或一组）任务线程
  * 线程内部串行化访问外设，避免多任务并发直接抢 I2C/SPI 造成竞争
  * 线程在无请求时阻塞/挂起，实现“局部启动，局部挂起”，节能
* 异步 + 回调
  * APP 只发请求（或注册回调），不阻塞等待
  * Handler 获取数据后调用 APP 的回调或发事件
  * 避免 APP 线程长时间等待 I2C 采样导致的系统调度抖动
* 容错
  * I2C Reset、数据校验、上次有效值回退
  * 心率测量限时（30s 超时）
  * Speaker 忙碌状态仲裁与优先级播放队列
  * MPU6050 双缓冲 DMA 避免丢中断
* 功耗协同
  * Handler 根据 UI 状态决定采样频率或者是否停止采样
  * 进入低功耗前 Handler 会自动降采样/停采样并释放外设
  * 唤醒后恢复

典型子模块：

* AHT21 温湿度 Handler
  * 软件 I2C
  * 数据新鲜度缓存，300ms 内的重复请求直接复用
  * 支持回调通知 UI

![[file-20260726212322928.png]]

![[file-20260726212352446.png]]

![[file-20260726212416089.png]]

![[file-20260726212425832.png]] ![[file-20260726212435374.png]] ![[file-20260726212455345.png]]

![[file-20260726212505182.png]]

* MPU6050 运动 Handler
  * 硬件 I2C + DMA + FIFO 中断
  * 双 buffer 和 Notify 机制
  * 步数/抬腕算法
  * 低功耗唤醒源

![[file-20260726212516625.png]]

* Speaker Handler
  * 三线程模型：事件接收/播放执行/播放完成跟踪
  * 互斥锁 + 优先级仲裁，保证音频事件不会乱序打断
  * Busy 引脚跟踪，确保播放完整性
* 显示/触摸 Handler
  * LCD SPI+DMA 刷屏
  * 触摸 IC 中断触发，实时注入 LVGL pointer 事件

  ![[file-20260726212551007.png]]

EM7028

![[file-20260726212609047.png]]

* CST816T

![[file-20260726212625388.png]]

* W25Q64

![[file-20260726212645480.png]]

* DimmingCurrent

![[file-20260726212730481.png]]

* SPL06

![[file-20260726212750667.png]]

* 电池/ADC Handler
  * ADC 单次采样 + 多点滤波 + 卡尔曼/均值滤波
  * 电压→电量查表
  * 上报 EVENT\_LOW\_BATTERY

BSP 层对 APP 来说，就像一组“服务对象”；

对 MCU 来说，是一组受控、低功耗友好的“资源管理器”。

# 6. MCU 层程序架构

MCU 层程序代表 “跟 STM32F411 硬件最贴身的那一层”。

主要模块：

* 系统启动流程
  * 上电/复位进入 Bootloader（位于 Flash 起始 16KB 或 32KB）
  * Bootloader 校验外部 Flash 分区 A/B 的固件与标志位，决定是否要升级

  1. 如需升级：

     * 将外部 Flash 中加密固件分片解密搬运到片上 Flash APP 区
     * 备份旧 APP（回滚保护）
     * 设置成功/失败标志

  2. 跳转 APP（注意：

     * 先反向关闭当前时钟、外设
     * 重设中断向量表到 APP 基地址
     * 再初始化 APP 时钟树，避免 PLL 写不进的问题）
* 时钟/电源控制
  * 系统时钟配置、降频策略
  * 低功耗入口 HAL\_PWR\_EnterSTOPMode(...)
  * STOP 唤醒恢复时钟、恢复外设工作模式
* 看门狗
  * IWDG 独立看门狗初始化
  * 看门狗喂养接口
  * 异常复位时记录原因
* 中断向量表重定位
  * Bootloader 和 APP 使用不同向量表
  * APP 启动时将 VTOR 指向自身
* 存储分区表
  * STM32 内部 Flash：Bootloader, APP 固件区
  * 外部 W25Q64：OTA 缓存 A 区/B 区、日志区、配置区
  * EEPROM/配置区：升级 Flag、传感器校准系数等
* cmbacktrace / 错误日志持久化
  * HardFault/MemManage 等异常发生时抓栈、寄存器信息
  * 写入日志区（外部 Flash/内部 Flash 保留区）
  * 用于后续调试和迭代

这个层基本不直接服务 UI，而是保障系统能安全启动、稳定运行、可恢复、可升级。

# 7. Middleware 层程序架构

Middleware 层位于 APP 和 BSP 之间，为“多模块共用的逻辑”提供统一实现。

核心子系统：

7.1 LVGL 适配子系统

* LVGL display driver：调用 display\_interface\_t.flush()
* LVGL input driver：从触摸 Handler 获取坐标、手势
* 亮屏/息屏策略：在低功耗模式下降低刷新频率和背光亮度
* UI 状态机：页面 enter()/exit() 与 EVENT\_UI\_SWITCH 配合

7.2 运动/健康算法子系统

* 从 MPU6050 Handler 接收批量加速度数据（DMA 双 buffer 结果）
* 滤波（均值/滑动窗口）
* 步数统计、防抖动阈值
* 抬腕识别（触发屏幕点亮）
* 心率稳定性判断（滑动平均 + 区间校验）

7.3 OTA 子系统

* BLE 透传（nRF52840 负责 BLE 协议栈和 Ymodem 发送）
* STM32 侧 UART 串口接收分片数据到环形缓冲区 → 写入外部 Flash A 区
* 每包 CRC 校验
* 完成后设置升级 Flag
* 重启后由 Bootloader 完成解密、搬运、回滚保护
* 若新固件启动失败或看门狗长时间未被喂 → Bootloader 回滚旧固件

7.4 存储/配置子系统

* 提供 config\_load/save() 给 APP
* 提供 flash\_xxx() 给 OTA/日志模块
* 管理分区和磨损（后续可扩展）

7.5 安全子系统

* AES-128 解密固件 S
* Hash/CRC 校验固件完整性
* 将这些逻辑限制在 Bootloader+Middleware，保证固件可信启动

7.6 日志与错误报告子系统

* 错误任务（Error Task）+ 看门狗任务（Watchdog Task）+ 日志任务（Log Task）
* 日志压缩加密后写入 Flash
* 用于量产/现场回收故障信息，支撑 DevOps 闭环（线上收集→分析→OTA 修复）

8. OS 层程序架构

# 8. OS 层程序架构

OS 层负责让所有这些线程安全、有序、可预期地运行。

8.1 任务管理

* 每个 Handler（如 MPU6050、Speaker、AHT21）可以有自己的服务线程；
* UI 任务负责 LVGL tick 与 UI 刷新；
* OTA 任务负责固件下行与写 Flash；
* Power 管理任务负责空闲计时，触发模式切换；
* Watchdog 任务定期检查所有关键任务心跳；
* Error 任务在收到错误后负责记录日志、必要时重启模块或触发系统软复位。

8.2 IPC 与调度

* 队列：面向数据生产/消费
* 事件组：面向系统状态同步
* Notify：面向高实时（中断→线程）
* Mutex/信号量：面向资源保护和同步
* 优先级继承（互斥锁）防止优先级反转导致死锁

8.3 Tickless/低功耗协作

* Idle Hook 中，如果满足无操作超时，就让系统切入更低功耗模式（降频、停止非关键采样等）
* 20s 无交互：降频 + 进入 Tickless
* 更长无交互：STOP 模式（由 MPU6050 中断/触摸/按键唤醒）

8.4 内存管理

* FreeRTOS heap\_4 方案
* 静态内存池（sensor\_data\_pool 等）避免碎片
* 栈水位监控与栈溢出钩子
* 关键调度/ISR 放入 SRAM，配合 SCT 分散加载

8.5 异常监控和恢复

* 如果某任务长时间未喂狗 → Watchdog 任务上报 Error 任务 → Error 任务尝试恢复该模块或触发系统复位
* 系统复位后 Bootloader 会识别异常启动，必要时执行回滚

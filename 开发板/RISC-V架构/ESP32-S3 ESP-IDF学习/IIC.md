### IIC 基本参数

(1) 速率：[[I2C]] 总线有标准模式（100kbit/s）和快速模式（400kbit/s）两种传输模式，还有更快的扩展模式和高速模式可供选择。

(2) 器件地址：每个设备都有唯一的 7 位或 10 位地址，可以通过地址选择来确定与谁进行通信。

(3) 总线状态：[[I2C]] 总线有五种状态，分别是空闲状态、起始信号、结束信号、响应信号、数据传输。

(4) 数据格式：[[I2C]] 总线有两种数据格式，标准格式和快速格式。标准格式是 8 位数据字节加上 1 位 ack/nack（应答/非应答）位，快速格式允许两个字节同时传输。

由于 SCL 和 SDA 线是双向的，它们也可能会由于外部原因（比如线路中的电容等）出现电平误差，而从而导致通信出错。因此，在 IIC 总线中，通常使用上拉电阻来保证信号线在空闲状态下的电平为高电平。

### IIC_EXIO 函数解析

要使用此功能，需要导入必要的头文件：

#include "driver/[[I2C]].h"

esp_err_t i2c_param_config(i2c_port_t i2c_num, const i2c_config_t *i2c_conf);

	该函数用给定的配置，来配置 IIC 总线

	参数表

		![[Pasted image 20250928210805.png]]

		![[Pasted image 20250928210900.png]]

esp_err_t i2c_driver_install(i2c_port_t i2c_num,i2c_mode_t mode,size_t slv_rx_buf_len, size_t slv_tx_buf_len,int intr_alloc_flags);

	该函数设置某个管脚的中断服务函数

	参数表

		![[Pasted image 20250928211027.png]]

IIC 读写操作

	![[Pasted image 20250928211159.png]]

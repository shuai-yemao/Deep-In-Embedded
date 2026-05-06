 ### SPI_LCD 函数解析

需要导入必要的头文件：

#include "driver/spi_master.h"

esp_err_t spi_bus_initialize(spi_host_device_t host_id,const spi_bus_config_t *bus_config,spi_dma_chan_t dma_chan);

	该函数用于初始化 SPI 总线，并配置其 GPIO引脚和主模式下的时钟等参数

	参数表

		![[Pasted image 20251007161315.png]]

		![[Pasted image 20251007161326.png]]

esp_err_t spi_bus_add_device(spi_host_device_t host_id,const spi_device_interface_config_t *dev_config,spi_device_handle_t *handle);

	该函数用于在 SPI 总线上分配设备

	参数表

		![[Pasted image 20251007161514.png]]

		![[Pasted image 20251007161532.png]]

数据传输

![[Pasted image 20251007161612.png]]

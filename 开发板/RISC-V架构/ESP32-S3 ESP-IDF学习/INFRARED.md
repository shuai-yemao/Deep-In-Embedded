### RMT函数解析
需要导入必要的头文件：#include "driver/rmt_rx.h"

esp_err_t rmt_new_rx_channel(const rmt_rx_channel_config_t *config,
 rmt_channel_handle_t *ret_chan);
	 该函数用于安装 RMT 接收通道
	 参数表
		 ![[Pasted image 20251013202042.png]]
esp_err_t rmt_rx_register_event_callbacks(rmt_channel_handle_t rx_channel,const rmt_rx_event_callbacks_t *cbs,void *user_data);
	该函数用于配置 RMT 接收通道的回调函数
	参数表
		![[Pasted image 20251013202608.png]]
esp_err_t rmt_new_ir_nec_encoder(const ir_nec_encoder_config_t *config,rmt_encoder_handle_t *ret_encoder);
	该函数用于创建一个基于 NEC 协议的 RMT 编码器
	参数表
		![[Pasted image 20251013202640.png]]
esp_err_t rmt_enable(rmt_channel_handle_t channel);
	该函数用于使能 RMT 接收通道
	参数表
		![[Pasted image 20251013202739.png]]
esp_err_t rmt_receive(rmt_channel_handle_t rx_channel,void *buffer,size_t buffer_size,const rmt_receive_config_t *config);
	该函数用于启动 RMT 接收通道的接收任务
	参数表
		![[Pasted image 20251013202819.png]]
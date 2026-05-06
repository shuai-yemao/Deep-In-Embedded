### 程序流程图
![[Pasted image 20251026210643.png]]
### RGB-LCD函数解析
需要导入必要的头文件：
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"

esp_err_t esp_lcd_new_rgb_panel(const esp_lcd_rgb_panel_config_t*rgb_panel_config, esp_lcd_panel_handle_t *ret_panel);
	该函数通过配置结构体参数的方式将参数以指针的方式传进创建的 RGB 对象
	参数表
		![[Pasted image 20251026210819.png]]
		结构体定义
			![[Pasted image 20251026210844.png]]
esp_err_t
esp_lcd_panel_reset(esp_lcd_panel_handle_t panel);
	在创建 RGB 屏幕对象后需要进行 RGB 屏幕复位
	参数表
		![[Pasted image 20251026210956.png]]
esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t panel);
	通过上两个步骤的配置，可以对屏幕进行初始化了
	参数表
		![[Pasted image 20251026211106.png]]


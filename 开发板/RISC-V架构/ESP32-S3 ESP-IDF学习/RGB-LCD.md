### 程序流程图
![[assets/RGB-LCD/file-20260421201557224.png]]
### RGB-LCD函数解析
需要导入必要的头文件：
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"

esp_err_t esp_lcd_new_rgb_panel(const esp_lcd_rgb_panel_config_t*rgb_panel_config, esp_lcd_panel_handle_t *ret_panel);
	该函数通过配置结构体参数的方式将参数以指针的方式传进创建的 RGB 对象
	参数表
		![[assets/RGB-LCD/file-20260421201557227.png]]
		结构体定义
			![[assets/RGB-LCD/file-20260421201557230.png]]
esp_err_t
esp_lcd_panel_reset(esp_lcd_panel_handle_t panel);
	在创建 RGB 屏幕对象后需要进行 RGB 屏幕复位
	参数表
		![[assets/RGB-LCD/file-20260421201557233.png]]
esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t panel);
	通过上两个步骤的配置，可以对屏幕进行初始化了
	参数表
		![[assets/RGB-LCD/file-20260421201557236.png]]

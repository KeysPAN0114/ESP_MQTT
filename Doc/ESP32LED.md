# ESP32与其他客户端交互

## ESP32增加LED驱动
    在ESP-IDF增加官方的LEDC驱动，该驱动是PWM驱动LED灯。  
![led配置](./img-3/ESP32_1.png)
添加完驱动后，在main文件中添加led部分代码（完整代码参考文件）：
```c
    #include "driver/ledc.h"
    ...
    /* 配置 LED PWM (LEDC) 用于 IO3 调光 */
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .gpio_num       = LED_GPIO_PIN,
        .duty           = LEDC_MAX_DUTY / 2,   /* 初始 50% 亮度 */
        .hpoint         = 0,
    };
    ledc_channel_config(&ledc_channel);

    /* 安装 LEDC 渐变功能 */
    ledc_fade_func_install(0);
    ESP_LOGI(TAG, "LED on GPIO%d PWM initialized at 50%% brightness (fade enabled)", LED_GPIO_PIN);
```

## JSON格式的上报与下发
json格式文本是一种通用的配置，消息收发格式。例如项目中的led开关：{light:x} x为0,1
温湿度上报：{"temp":%.2f,"humi":%.2f}

详细的内容可以参考cJSON库。

## 主题的订阅与收发
ESP32与其他客户端的交互主要是通过MQTT的主题订阅与推送。目前项目中ESP32订阅的是/topic/appcli/up；其他客户端订阅ESP32发布消息的主题：/topic/espcli/up。
其他客户端的内容就不过多介绍了，可以使用AI生成一个对应的客户端即可，具体的开发方式与ESP32的开发方式大差不差。

## 效果
![客户端界面](./img-3/app_1.png)

这个客户端的功能包括：订阅ESP32的主题，接收温湿度数据，并且解析显示；按键控制ESP32的LED亮灭。
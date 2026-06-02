# ESP32 上报芯片温度到 EMQX 平台

> 本文档介绍如何使用 ESP32 内置温度传感器采集芯片温度，并通过 MQTT 协议上报至 EMQX 平台，同时实现双向通信。

---

## 目录

- [ESP32 上报芯片温度到 EMQX 平台](#esp32-上报芯片温度到-emqx-平台)
  - [目录](#目录)
  - [获取芯片温度并且上报](#获取芯片温度并且上报)
    - [1. 配置项目](#1-配置项目)
    - [2. 修改代码](#2-修改代码)
    - [3. 验证数据](#3-验证数据)
  - [ESP32 订阅其他客户端发送的数据](#esp32-订阅其他客户端发送的数据)

---

## 获取芯片温度并且上报

创建 ESP32 的温度传感器，并且获取芯片温度。

### 1. 配置项目

在工程的 `main` 文件夹 [`CMakeLists.txt`](../Software/ssl/main/CMakeLists.txt) 中添加组件 `esp_driver_tsens`：

![项目配置](./img-2/esp-1.png)

### 2. 修改代码

在 [`app_main.c`](../Software/ssl/main/app_main.c) 文件中修改代码，以正常使用芯片内部的温度传感器：

```c
esp_mqtt_client_handle_t global_client; // 完整代码请参考 github 项目文件相关部分

void temp_task(void *p) {
    ESP_LOGI(TAG, "Install temperature sensor, expected temp range: 10~50 ℃");
    temperature_sensor_handle_t temp_sensor = NULL;
    temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor_config, &temp_sensor));

    ESP_LOGI(TAG, "Enable temperature sensor");
    ESP_ERROR_CHECK(temperature_sensor_enable(temp_sensor));

    float tsens_value;
    int count = 0;
    while (1) {
        ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_sensor, &tsens_value));
        ESP_LOGI(TAG, "Temperature value %.02f ℃", tsens_value);
        vTaskDelay(pdMS_TO_TICKS(1000));
        count++;
        if (count == 5) {
            char buf[20];
            sprintf(buf, "%.02f", tsens_value);
            esp_mqtt_client_publish(global_client, "/topic/qos0/updata", buf, 0, 0, 0);
            count = 0;
        }
    }
}
```

> **说明：** 每隔 5 秒（每秒采集一次，第 5 次时上报）将温度数据通过 MQTT 发布到 `/topic/qos0/updata` 主题。

### 3. 验证数据

编译烧录后，在 EMQX 后台可以查看 ESP32 上报的数据：

![后台查看数据](./img-2/emqx-1.png)

连接后点击 **订阅**，即可实时查看 ESP32 上报的温度数据：

![后台订阅查看数据](./img-2/emqx-2.png)

---

## ESP32 订阅其他客户端发送的数据

这部分的代码在 ESP32 的例程里面已经存在了，所以只需要在后台的在线调试发送数据到 ESP32 订阅的主题即可。

**ESP32 订阅的主题：**

![ESP32 订阅主题](./img-2/esp-2.png)

**在后台的在线调试向 ESP32 订阅的主题发送数据：**

![后台发送数据](./img-2/emqx-3.png)

**在 ESP32 调试日志中查看后台发送的数据：**

![ESP32 接收数据](./img-2/esp-3.png)

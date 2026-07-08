/* MQTT over SSL Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "esp_system.h"
#include "esp_partition.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_tls.h"
#include "esp_ota_ops.h"
#include <sys/param.h>

#include "driver/temperature_sensor.h"
#include "driver/ledc.h"

#include <aht.h>
#include <i2cdev.h>

#define LED_GPIO_PIN        GPIO_NUM_3
#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL        LEDC_CHANNEL_0
#define LEDC_DUTY_RES       LEDC_TIMER_10_BIT  /* 10-bit 分辨率: 0~1023 */
#define LEDC_FREQUENCY      (5000)              /* PWM 频率 5kHz */
#define LEDC_MAX_DUTY       ((1 << 10) - 1)     /* 1023 */
#define LED_FADE_MS         (1000)              /* 渐变时长 1 秒 */

static const char *TAG = "mqtts_example";

static volatile uint32_t s_current_duty = LEDC_MAX_DUTY / 2;  /* 当前亮度占空比 */
static volatile bool     s_led_on = true;                      /* 灯当前开关状态 */

/**
 * @brief 立即设置 LED 亮度（无渐变）
 * @param percent 亮度百分比 0~100，0 为熄灭，100 为最亮
 */
void led_set_brightness(uint32_t percent)
{
    if (percent > 100) percent = 100;
    uint32_t duty = (LEDC_MAX_DUTY * percent) / 100;
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    s_current_duty = duty;
    s_led_on = (percent > 0);
    ESP_LOGI(TAG, "LED brightness set to %lu%% (duty=%lu)", percent, duty);
}

/**
 * @brief 渐变设置 LED 亮度
 * @param target_percent 目标亮度百分比 0~100
 * @param fade_time_ms   渐变时长（毫秒）
 */
void led_fade_to(uint32_t target_percent, uint32_t fade_time_ms)
{
    if (target_percent > 100) target_percent = 100;
    uint32_t target_duty = (LEDC_MAX_DUTY * target_percent) / 100;

    ledc_set_fade_with_time(LEDC_MODE, LEDC_CHANNEL, target_duty, fade_time_ms);
    ledc_fade_start(LEDC_MODE, LEDC_CHANNEL, LEDC_FADE_NO_WAIT);

    s_current_duty = target_duty;
    s_led_on = (target_percent > 0);
    ESP_LOGI(TAG, "LED fading to %lu%% (duty=%lu) in %lu ms", target_percent, target_duty, fade_time_ms);
}

/**
 * @brief 渐变开灯（从当前亮度渐变到 100%）
 */
void led_fade_on(void)
{
    led_fade_to(100, LED_FADE_MS);
}

/**
 * @brief 渐变关灯（从当前亮度渐变到 0%）
 */
void led_fade_off(void)
{
    led_fade_to(0, LED_FADE_MS);
}

/**
 * @brief 通过 light 值控制灯的渐变开关
 * @param light 0=渐灭，非0=渐亮
 */
void led_light_control(int light)
{
    if (light) {
        ESP_LOGI(TAG, "Light ON (fading in)");
        led_fade_on();
    } else {
        ESP_LOGI(TAG, "Light OFF (fading out)");
        led_fade_off();
    }
}


#if CONFIG_BROKER_CERTIFICATE_OVERRIDDEN == 1
static const uint8_t mqtt_eclipseprojects_io_pem_start[]  = "-----BEGIN CERTIFICATE-----\n" CONFIG_BROKER_CERTIFICATE_OVERRIDE "\n-----END CERTIFICATE-----";
#else
extern const uint8_t mqtt_eclipseprojects_io_pem_start[]   asm("_binary_mqtt_eclipseprojects_io_pem_start");
#endif
extern const uint8_t mqtt_eclipseprojects_io_pem_end[]   asm("_binary_mqtt_eclipseprojects_io_pem_end");

esp_mqtt_client_handle_t global_client;

//
// Note: this function is for testing purposes only publishing part of the active partition
//       (to be checked against the original binary)
//
static void send_binary(esp_mqtt_client_handle_t client)
{
    esp_partition_mmap_handle_t out_handle;
    const void *binary_address;
    const esp_partition_t *partition = esp_ota_get_running_partition();
    esp_partition_mmap(partition, 0, partition->size, ESP_PARTITION_MMAP_DATA, &binary_address, &out_handle);
    // sending only the configured portion of the partition (if it's less than the partition size)
    int binary_size = MIN(CONFIG_BROKER_BIN_SIZE_TO_SEND, partition->size);
    int msg_id = esp_mqtt_client_publish(client, "/topic/binary", binary_address, binary_size, 0, 0);
    ESP_LOGI(TAG, "binary sent with msg_id=%d", msg_id);
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    global_client = client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_subscribe(client, "/topic/appcli/up", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
        ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d, return code=0x%02x ", event->msg_id, (uint8_t)*event->data);
        msg_id = esp_mqtt_client_publish(client, "/topic/espcli/up", "data", 0, 0, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);

        /* 处理 /topic/appcli/up 主题的 light 控制指令 */
        if (event->topic_len == (int)strlen("/topic/appcli/up") &&
            strncmp(event->topic, "/topic/appcli/up", event->topic_len) == 0) {
            /* 将 data 复制到 null-terminated 缓冲区以便安全解析 */
            char data_buf[256];
            int copy_len = event->data_len < (int)(sizeof(data_buf) - 1) ? event->data_len : (int)(sizeof(data_buf) - 1);
            memcpy(data_buf, event->data, copy_len);
            data_buf[copy_len] = '\0';

            /* 解析 {light:x} 格式，x 为 0 或非0 */
            const char *light_key = strstr(data_buf, "\"light\"");
            if (light_key) {
                /* 跳过 "light" 和冒号 */
                const char *colon = strchr(light_key, ':');
                if (colon) {
                    colon++;  /* 跳过 ':' */
                    /* 跳过空格 */
                    while (*colon == ' ') colon++;
                    int light_val = atoi(colon);
                    ESP_LOGI(TAG, "Received light command: %d", light_val);
                    led_light_control(light_val);
                }
            }
            /* 回复同样的数据到上行主题 */
            esp_mqtt_client_publish(global_client, "/topic/espcli/up", data_buf, 0, 0, 0);
            ESP_LOGI(TAG, "Light control echo sent: %s", data_buf);
        }

        if (strncmp(event->data, "send binary please", event->data_len) == 0) {
            ESP_LOGI(TAG, "Sending the binary");
            send_binary(client);
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGI(TAG, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
            ESP_LOGI(TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
            ESP_LOGI(TAG, "Last captured errno : %d (%s)",  event->error_handle->esp_transport_sock_errno,
                     strerror(event->error_handle->esp_transport_sock_errno));
        } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGI(TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
        } else {
            ESP_LOGW(TAG, "Unknown error type: 0x%x", event->error_handle->error_type);
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void)
{
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri = CONFIG_BROKER_URI,
            .verification.certificate = (const char *)mqtt_eclipseprojects_io_pem_start
        },
        .credentials = {
            .username = "esp_client",
            .client_id = "esp_client_1",
            .authentication.password = "12345678",
        },
    };

    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

void temp_task(void *p) {
    ESP_LOGI(TAG, "Install temperature sensor, expected temp ranger range: 10~50 ℃");
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
        // count++;
        // if (count == 5) {
        //     char buf[20];
        //     sprintf(buf, "%.02f", tsens_value);
            // esp_mqtt_client_publish(global_client, "/topic/espcli/up", buf, 0, 0, 0);
        //     count = 0;
        // }
    }
}

/* ======================== AHT10 Task (使用 esp-idf-lib/aht 组件) ======================== */

/*
 * ESP32-C3 引脚配置
 * 安全可用: GPIO_2, GPIO_3, GPIO_4, GPIO_5, GPIO_18, GPIO_19
 * 禁止使用: GPIO_6~11 (SPI Flash)
 */
#define AHT10_I2C_PORT      I2C_NUM_0
#define AHT10_SDA_PIN       GPIO_NUM_5
#define AHT10_SCL_PIN       GPIO_NUM_6

void aht10_task(void *p)
{
    aht_t dev = { 0 };
    dev.type = AHT_TYPE_AHT1x;
    dev.mode = AHT_MODE_NORMAL;

    /* 初始化 AHT10 I2C 描述符 */
    esp_err_t ret = aht_init_desc(&dev, AHT_I2C_ADDRESS_GND, AHT10_I2C_PORT, AHT10_SDA_PIN, AHT10_SCL_PIN);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "aht_init_desc failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    /* 等待 AHT10 上电稳定（数据手册要求至少 20ms，实际建议 300ms+） */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* 初始化传感器（带重试，AHT10 首次通信可能失败） */
    bool init_ok = false;
    for (int retry = 0; retry < 5; retry++) {
        ret = aht_init(&dev);
        if (ret == ESP_OK) {
            init_ok = true;
            break;
        }
        ESP_LOGW(TAG, "aht_init attempt %d failed: %s, retrying...", retry + 1, esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    if (!init_ok) {
        ESP_LOGW(TAG, "aht_init failed after retries, will try to read anyway");
    }

    /* 检查校准状态 */
    bool busy = false, calibrated = false;
    ret = aht_get_status(&dev, &busy, &calibrated);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "AHT10 status: busy=%d, calibrated=%s", busy, calibrated ? "yes" : "no");
    }

    ESP_LOGI(TAG, "AHT10 initialized successfully");

    float temperature, humidity;
    int count = 0;
    while (1) {
        ret = aht_get_data(&dev, &temperature, &humidity);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "AHT10 -> Temp: %.2f ℃, Humidity: %.2f %%RH", temperature, humidity);
            count++;
            if (count >= 5) {
                char buf[64];
                snprintf(buf, sizeof(buf), "{\"temp\":%.2f,\"humi\":%.2f}", temperature, humidity);
                esp_mqtt_client_publish(global_client, "/topic/espcli/up", buf, 0, 0, 0);
                count = 0;
            }
        } else {
            ESP_LOGE(TAG, "AHT10 read failed: %s", esp_err_to_name(ret));
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("mqtt_example", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
    esp_log_level_set("transport", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    /* 初始化 i2cdev 库（esp-idf-lib 的 I2C 设备驱动必须先调用此函数） */
    esp_err_t i2c_ret = i2cdev_init();
    if (i2c_ret != ESP_OK) {
        ESP_LOGW(TAG, "i2cdev_init: %s (may already be initialized)", esp_err_to_name(i2c_ret));
    }

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

    mqtt_app_start();

    xTaskCreate(temp_task, "temp_task", 2048, NULL, 5, NULL);
    xTaskCreate(aht10_task, "aht10_task", 4096, NULL, 5, NULL);
}

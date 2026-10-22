#include "wifi_module.h"
#include "mqtt_module.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

// WiFi 账号密码放在 secrets.h（不进仓库），模板见 secrets.example.h
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

static const char *TAG = "WIFI_MODULE";

// 事件回调函数（类似于中断服务函数，当WiFi状态改变时系统自动调用）
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    // 如果 WiFi 硬件启动成功，就开始连接路由器
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi 模块已启动，正在连接路由器...");
    } 
    // 如果连接断开，一直尝试重新连接
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGW(TAG, "连接失败或断开，正在重连...");
    } 
    // 如果成功获取到 IP 地址，说明正式连上网了！
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "✅ 成功连上 WiFi! 分配到的 IP 地址: " IPSTR, IP2STR(&event->ip_info.ip));
        mqtt_module_init(); // 连上 WiFi 后，立即启动 MQTT 通信
    }
}

void wifi_init_sta(void)
{
    // 1. 初始化 NVS (非易失性存储)。WiFi 模块在底层必须用它来存一些射频校准数据
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. 初始化底层网络接口和事件循环（注册酒店前台和呼叫器）
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta(); // 创建默认的 STA 接口

    // 3. 使用默认配置初始化 WiFi 硬件底层
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 4. 将我们的"事件回调函数"注册到系统里，监听 WiFi 连上或断开的消息
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // 5. 配置账号密码
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK, // 设置最低安全级别
        },
    };

    // 6. 设置为 STA 模式，应用配置并启动硬件
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

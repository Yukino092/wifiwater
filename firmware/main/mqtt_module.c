#include "mqtt_module.h"
#include "mqtt_client.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include <stdio.h>
#include <string.h>

// 服务器地址与账号密码放在 secrets.h（不进仓库），模板见 secrets.example.h
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

static const char *TAG = "MQTT_MODULE";

// ===== 与手机 App 约定的服务器和话题（必须和 App 端保持一致）=====
// broker / 账号 / 密码 均来自 secrets.h
#define TOPIC_LOG        "esp32s3/chat/log"   // 发到这里 -> App 显示
#define TOPIC_CMD        "esp32s3/chat/cmd"   // App 发的指令会到这里

static esp_mqtt_client_handle_t client = NULL;
static bool connected = false;
static bool started = false;
static mqtt_cmd_handler_t s_cmd_handler = NULL; // 主程序注册的指令回调

// MQTT 事件回调：连接成功、收到数据等都会进这里
static void mqtt_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t e = event_data;

    switch (e->event_id) {
    case MQTT_EVENT_CONNECTED:
        connected = true;
        ESP_LOGI(TAG, "已连接 MQTT broker");
        esp_mqtt_client_subscribe(client, TOPIC_CMD, 0);
        ESP_LOGI(TAG, "已订阅指令话题: %s", TOPIC_CMD);
        mqtt_module_send("[BOOT] 设备已上线");
        break;

    case MQTT_EVENT_DISCONNECTED:
        connected = false;
        ESP_LOGW(TAG, "MQTT 断开（会自动重连）");
        break;

    case MQTT_EVENT_DATA: {
        // 取出 topic 字符串
        char topic[64] = {0};
        int tlen = e->topic_len;
        if (tlen > (int)sizeof(topic) - 1) tlen = sizeof(topic) - 1;
        memcpy(topic, e->topic, tlen);

        if (strcmp(topic, TOPIC_CMD) == 0) {
            // 取出指令文本
            char payload[128] = {0};
            int plen = e->data_len;
            if (plen > (int)sizeof(payload) - 1) plen = sizeof(payload) - 1;
            memcpy(payload, e->data, plen);

            ESP_LOGI(TAG, "收到 App 指令: %s", payload);

            // 调用主程序注册的指令回调（控制各类执行器）
            if (s_cmd_handler) {
                s_cmd_handler(payload);
            }

            // 回显给 App，让手机端看到"已收到"的确认
            char buf[160];
            snprintf(buf, sizeof(buf), "[CMD] %s", payload);
            mqtt_module_send(buf);
        }
        break;
    }

    default:
        break;
    }
}

void mqtt_module_init(void)
{
    if (started) return;

    // 用芯片 MAC 生成唯一 client_id，避免多台设备同时上线时冲突
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char client_id[32];
    snprintf(client_id, sizeof(client_id), "esp32_%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials.client_id = client_id,
        .credentials.username = MQTT_USERNAME,
        .credentials.authentication.password = MQTT_PASSWORD,
        // TLS 校验服务器证书（用内置 CA 证书库）
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
    };

    client = esp_mqtt_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "MQTT 客户端初始化失败");
        return;
    }

    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    esp_err_t err = esp_mqtt_client_start(client);
    if (err == ESP_OK) {
        started = true;
        ESP_LOGI(TAG, "MQTT 已启动, broker=%s", MQTT_BROKER_URI);
    } else {
        ESP_LOGE(TAG, "MQTT 启动失败: %s", esp_err_to_name(err));
    }
}

void mqtt_module_send(const char *msg)
{
    if (!client || !connected || !msg) return;
    int msg_id = esp_mqtt_client_publish(client, TOPIC_LOG, msg, 0, 0, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "发送失败");
    }
}

bool mqtt_module_is_connected(void)
{
    return connected;
}

void mqtt_module_set_cmd_handler(mqtt_cmd_handler_t handler)
{
    s_cmd_handler = handler;
}

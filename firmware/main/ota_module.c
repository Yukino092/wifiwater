#include "ota_module.h"
#include <string.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "OTA";

// 从 HTTP 地址下载固件并写入下一个 OTA 分区，成功后切换启动分区并重启
esp_err_t ota_start_from_url(const char *url)
{
    if (url == NULL || url[0] == '\0') {
        ESP_LOGE(TAG, "OTA 地址为空");
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "开始 OTA 升级: %s", url);

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 8000,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "HTTP 客户端初始化失败");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "打开连接失败: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length <= 0) {
        ESP_LOGE(TAG, "获取固件大小失败");
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "固件大小: %d 字节", content_length);

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "获取 OTA 分区失败");
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "写入分区: %s", update_partition->label);

    esp_ota_handle_t update_handle = 0;
    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin 失败: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    char buf[2048];
    int total = 0;
    int read = 0;
    while ((read = esp_http_client_read(client, buf, sizeof(buf))) > 0) {
        err = esp_ota_write(update_handle, buf, read);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA 写入失败: %s", esp_err_to_name(err));
            esp_ota_abort(update_handle);
            esp_http_client_cleanup(client);
            return err;
        }
        total += read;
    }
    ESP_LOGI(TAG, "下载完成: %d 字节", total);

    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA 结束失败: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "设置启动分区失败: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    esp_http_client_cleanup(client);
    ESP_LOGW(TAG, "OTA 升级成功，3 秒后重启...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    return ESP_OK;
}

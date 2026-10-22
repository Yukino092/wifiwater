#include "water_temp.h"
#include "esp_log.h"
#include <stdbool.h>
#include "driver/gpio.h"
// 引入官方下载好的 1-Wire 和 DS18B20 头文件
#include "onewire_bus.h"
#include "ds18b20.h"

static const char *TAG = "WATER_TEMP";

// 绑定 GPIO5
#define WATER_TEMP_GPIO GPIO_NUM_5

static onewire_bus_handle_t s_bus = NULL;
static ds18b20_device_handle_t s_ds18b20_handle = NULL;

void water_temp_init(void)
{
    ESP_LOGI(TAG, "开始初始化 DS18B20 (GPIO5)...");

    // 1. 初始化 1-Wire 基础总线 (ESP32-S3 用 RMT 外设生成精准时序)
    onewire_bus_config_t bus_config = {
        .bus_gpio_num = WATER_TEMP_GPIO,
    };
    onewire_bus_rmt_config_t rmt_config = {
        .max_rx_bytes = 10, // 接收缓冲大小
    };
    if (onewire_new_bus_rmt(&bus_config, &rmt_config, &s_bus) != ESP_OK) {
        ESP_LOGE(TAG, "创建 1-Wire 总线失败");
        return;
    }

    // 2. 不枚举、直接绑定（Skip ROM 模式，适用于总线上只有一只 DS18B20）。
    //    之前的做法是先扫描 ROM，而扫描在超时后会让 RMT 通道卡死，
    //    导致「重启后一旦扫描失败就永久 -999、必须重新上电」。
    //    这里改成不扫描：句柄始终有效，读失败就下个周期重试，无需重新上电。
    ds18b20_config_t ds_cfg = {0};
    if (ds18b20_new_device_from_bus(s_bus, &ds_cfg, &s_ds18b20_handle) == ESP_OK) {
        ESP_LOGI(TAG, "DS18B20 驱动加载完成（Skip ROM 模式，读取失败自动重试）");
    } else {
        ESP_LOGE(TAG, "创建 DS18B20 设备失败");
    }
}

float water_temp_get_celsius(void)
{
    // 没找到设备，返回错误值
    if (s_ds18b20_handle == NULL) {
        return -999.0f;
    }

    float temperature = 0.0;

    // 触发一次转换（内部会等待约 800ms 让 12bit 转换完成）
    if (ds18b20_trigger_temperature_conversion(s_ds18b20_handle) != ESP_OK) {
        return -999.0f;
    }
    // 读结果；读失败返回 -999（不崩溃），下个周期再试
    if (ds18b20_get_temperature(s_ds18b20_handle, &temperature) != ESP_OK) {
        return -999.0f;
    }

    return temperature;
}

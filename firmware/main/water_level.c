#include "water_level.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "WATER_LEVEL";

// 两个数字水位传感器：内部上拉，开关闭合到 GND 时读到低电平(0)
#define WL1_PIN GPIO_NUM_9
#define WL2_PIN GPIO_NUM_38

void water_level_init(void)
{
    // 传感器1（GPIO9）
    gpio_reset_pin(WL1_PIN);
    gpio_set_direction(WL1_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(WL1_PIN, GPIO_PULLUP_ONLY);

    // 传感器2（GPIO38）
    gpio_reset_pin(WL2_PIN);
    gpio_set_direction(WL2_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(WL2_PIN, GPIO_PULLUP_ONLY);

    ESP_LOGI(TAG, "水位传感器初始化: 1号=GPIO9, 2号=GPIO38 (数字输入, 内部上拉)");
}

int water_level_get_level1(void)
{
    return gpio_get_level(WL1_PIN);
}

int water_level_get_level2(void)
{
    return gpio_get_level(WL2_PIN);
}

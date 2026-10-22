#include "water_pump.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "WATER_PUMP";

// 绑定 GPIO12 控制水泵
#define WATER_PUMP_PIN GPIO_NUM_12

// 继电器触发方式：高电平触发（高=继电器吸合=水泵通电）
void water_pump_init(void)
{
    ESP_LOGI(TAG, "初始化水泵控制引脚 (GPIO12)...");

    // 1. 复位引脚以清除任何历史配置
    gpio_reset_pin(WATER_PUMP_PIN);

    // 2. 将引脚配置为推挽输出模式
    gpio_set_direction(WATER_PUMP_PIN, GPIO_MODE_OUTPUT);

    // 3. 高电平触发：上电默认拉低 = 继电器断开 = 水泵关闭（安全）
    gpio_set_level(WATER_PUMP_PIN, 0);
}

void water_pump_set_state(int state)
{
    // 高电平触发：1=开（引脚高，继电器吸合） 0=关（引脚低，继电器断开）
    gpio_set_level(WATER_PUMP_PIN, state ? 1 : 0);
}

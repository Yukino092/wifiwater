#include "refill_pump.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "REFILL_PUMP";

// 绑定 GPIO11 控制补水泵（与加热棒对调后的位置）
#define REFILL_PUMP_PIN GPIO_NUM_11

// 继电器触发方式：高电平触发（高=继电器吸合=补水泵通电补水）
void refill_pump_init(void)
{
    ESP_LOGI(TAG, "初始化补水泵控制引脚 (GPIO11)...");

    // 1. 复位引脚以清除任何历史配置
    gpio_reset_pin(REFILL_PUMP_PIN);

    // 2. 将引脚配置为推挽输出模式
    gpio_set_direction(REFILL_PUMP_PIN, GPIO_MODE_OUTPUT);

    // 3. 高电平触发：上电默认拉低 = 继电器断开 = 补水泵关闭（安全）
    gpio_set_level(REFILL_PUMP_PIN, 0);
}

void refill_pump_set_state(int state)
{
    // 高电平触发：1=开（引脚高，继电器吸合） 0=关（引脚低，继电器断开）
    gpio_set_level(REFILL_PUMP_PIN, state ? 1 : 0);
}

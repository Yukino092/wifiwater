#include "heater.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "HEATER";

// 绑定 GPIO8 控制继电器（与补水泵对调后的位置）
#define HEATER_PIN GPIO_NUM_8

// 继电器触发方式：高电平触发（高=继电器吸合=加热棒通电）
void heater_init(void)
{
    ESP_LOGI(TAG, "初始化加热棒控制引脚 (GPIO8)...");

    // 1. 复位引脚状态
    gpio_reset_pin(HEATER_PIN);

    // 2. 配置为推挽输出模式
    gpio_set_direction(HEATER_PIN, GPIO_MODE_OUTPUT);

    // 3. 高电平触发：上电默认拉低 = 继电器断开 = 加热棒关闭（安全）
    gpio_set_level(HEATER_PIN, 0);
}

void heater_set_state(int state)
{
    // 高电平触发：1=开（引脚高，继电器吸合） 0=关（引脚低，继电器断开）
    gpio_set_level(HEATER_PIN, state ? 1 : 0);
}
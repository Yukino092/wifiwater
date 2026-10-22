#include "buzzer.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "BUZZER";

// ç»‘å®š GPIO10
#define BUZZER_PIN GPIO_NUM_10

void buzzer_init(void)
{
    ESP_LOGI(TAG, "åˆå§‹åŒ–èœ‚é¸£å™¨ (GPIO10)...");

    // 1. å¤ä½å¼•è„šä»¥æ¸…é™¤ä»»ä½•åŽ†å²é…ç½®
    gpio_reset_pin(BUZZER_PIN);
    
    // 2. å°†å¼•è„šé…ç½®ä¸ºçº¯è¾“å‡ºæ¨¡å¼
    gpio_set_direction(BUZZER_PIN, GPIO_MODE_OUTPUT);
    
    // 3. åˆå§‹çŠ¶æ€æ‹‰ä½Žï¼Œé»˜è®¤å…³é—­èœ‚é¸£å™¨
    // æ³¨æ„ï¼šå¦‚æžœä½ ä¹°çš„æ˜¯â€œä½Žç”µå¹³è§¦å‘â€çš„èœ‚é¸£å™¨ï¼Œè¿™é‡Œåˆå§‹çŠ¶æ€åº”è®¾ä¸º 1 å…³é—­
    gpio_set_level(BUZZER_PIN, 1); // 低电平触发，拉高=关闭
}

void buzzer_set_state(int state)
{
    // å°†ä¼ å…¥çš„çŠ¶æ€å€¼ï¼ˆ1 æˆ– 0ï¼‰ç›´æŽ¥å†™å…¥åˆ°åº•å±‚ç¡¬ä»¶å¼•è„š
    gpio_set_level(BUZZER_PIN, !state); // 低电平触发，取反
}

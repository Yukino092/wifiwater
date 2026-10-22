#ifndef _WS2812_H_
#define _WS2812_H_

#include <stdint.h>
#include "esp_err.h"

// 初始化 WS2812 灯带（GPIO3）
esp_err_t ws2812_init(void);

// 设置第 index 颗灯的颜色（参数顺序是 R, G, B，各 0~255）
esp_err_t ws2812_set_led(uint32_t index, uint8_t r, uint8_t g, uint8_t b);

// 设置所有灯为同一颜色（R, G, B）
esp_err_t ws2812_set_all(uint8_t r, uint8_t g, uint8_t b);

// 关闭所有灯（全黑）
esp_err_t ws2812_clear(void);

#endif // _WS2812_H_

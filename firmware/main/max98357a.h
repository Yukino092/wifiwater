#ifndef _MAX98357A_H_
#define _MAX98357A_H_

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// 初始化 I2S 音频输出（MAX98357A 数字功放）
// 成功返回 ESP_OK；重复调用会直接返回 ESP_OK（防重复初始化）
esp_err_t max98357a_init(void);

// 播放一段 PCM 数据（16bit 单声道）
// data: 采样数据缓冲区；samples: 采样点个数
esp_err_t max98357a_write(const int16_t *data, size_t samples);

// 播放一段正弦波测试音（用于验证喇叭是否接好）
// freq_hz: 频率；duration_ms: 时长
esp_err_t max98357a_test_tone(uint16_t freq_hz, uint16_t duration_ms);

// 自动扫描 I2S 引脚顺序：依次尝试 6 种引脚组合并播放不同频率测试音
// 用于找出 BCLK/LRCK/DIN 的真实接线顺序
void max98357a_pin_scan(void);

#endif // _MAX98357A_H_


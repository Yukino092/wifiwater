#ifndef _INMP441_H_
#define _INMP441_H_

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

// 初始化 INMP441 麦克风（I2S 输入，16kHz，立体声格式，取左声道）
esp_err_t inmp441_init(void);

// 读取 samples 个采样点（24bit 左对齐存储在 int32 里）
esp_err_t inmp441_read(int32_t *buf, size_t samples);

// 读取一小段并计算平均音量（0~100）
int inmp441_read_volume(void);

// 诊断：读一段立体声数据，分别打印左/右声道原始峰值（排查接线/通道问题）
void inmp441_debug_channels(void);

#endif // _INMP441_H_

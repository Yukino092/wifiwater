#ifndef _TURBIDITY_H_
#define _TURBIDITY_H_

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// 浊度 (Turbidity) 模块专属的上下文结构体
typedef struct {
    adc_oneshot_unit_handle_t unit_handle; // 存放共享的 ADC1 网点句柄
    adc_cali_handle_t cali_handle;         // 浊度传感器专属的校准句柄
} turbidity_context_t;

// 初始化和读取函数声明
void turbidity_init(turbidity_context_t *turb_ctx);
int turbidity_get_voltage_mv(turbidity_context_t *turb_ctx);

#endif // _TURBIDITY_H_
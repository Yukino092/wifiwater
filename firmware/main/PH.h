#ifndef _PH_H_
#define _PH_H_

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// PH 模块专属的上下文结构体
typedef struct {
    adc_oneshot_unit_handle_t unit_handle; // 存放共享网点句柄
    adc_cali_handle_t cali_handle;         // PH 专属的校准句柄
} ph_context_t;

// 函数声明
void ph_init(ph_context_t *ph_ctx);
int ph_get_voltage_mv(ph_context_t *ph_ctx);

#endif // _PH_H_
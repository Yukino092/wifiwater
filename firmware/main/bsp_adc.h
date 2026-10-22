#ifndef _BSP_ADC_H_
#define _BSP_ADC_H_

#include "esp_adc/adc_oneshot.h"

/**
 * @brief 获取 ADC1 单元的共享句柄（带防重复初始化机制）
 * @return adc_oneshot_unit_handle_t ADC1的句柄
 */
adc_oneshot_unit_handle_t bsp_get_adc1_handle(void);

#endif // _BSP_ADC_H_
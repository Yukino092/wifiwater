#ifndef _TDS_H_
#define _TDS_H_

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

typedef struct {
    adc_oneshot_unit_handle_t unit_handle; 
    adc_cali_handle_t cali_handle;         
} tds_context_t;

// 参数变干净了，只需传自己的结构体指针
void tds_init(tds_context_t *tds_ctx);
int tds_get_voltage_mv(tds_context_t *tds_ctx);

#endif // _TDS_H_
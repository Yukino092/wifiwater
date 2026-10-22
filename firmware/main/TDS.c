#include "TDS.h"
#include "bsp_adc.h" // 引入公共资源管理器
#include "esp_log.h"

static const char *TAG = "TDS";

void tds_init(tds_context_t *tds_ctx)
{
    ESP_LOGI(TAG, "初始化 TDS 传感器 (GPIO7)...");

    // 1. 直接向公共管理器索要 ADC1 的句柄，完全自己搞定！
    tds_ctx->unit_handle = bsp_get_adc1_handle();

    // 2. 配置专属通道 (ADC_CHANNEL_6)
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT, 
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(tds_ctx->unit_handle, ADC_CHANNEL_6, &config));

    // 3. 配置专属校准
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = ADC_CHANNEL_6,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &tds_ctx->cali_handle));
}

int tds_get_voltage_mv(tds_context_t *tds_ctx)
{
    int adc_raw = 0;       
    int voltage_mv = 0;    
    ESP_ERROR_CHECK(adc_oneshot_read(tds_ctx->unit_handle, ADC_CHANNEL_6, &adc_raw));
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(tds_ctx->cali_handle, adc_raw, &voltage_mv));
    return voltage_mv;
}
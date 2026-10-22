#include "turbidity.h"
#include "bsp_adc.h" // 核心：引入公共资源管理器，去拿共享句柄
#include "esp_log.h"

static const char *TAG = "TURBIDITY";

void turbidity_init(turbidity_context_t *turb_ctx)
{
    ESP_LOGI(TAG, "初始化浊度传感器 (GPIO8)...");

    // 1. 直接向公共管理器索要 ADC1 的句柄
    turb_ctx->unit_handle = bsp_get_adc1_handle();

    // 2. 配置专属通道 (ADC_CHANNEL_7 对应 GPIO8)
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT, 
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(turb_ctx->unit_handle, ADC_CHANNEL_7, &config));

    // 3. 配置专属校准点钞员
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = ADC_CHANNEL_7, // 这里配置通道 7
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &turb_ctx->cali_handle));
}

int turbidity_get_voltage_mv(turbidity_context_t *turb_ctx)
{
    int adc_raw = 0;       
    int voltage_mv = 0;    
    
    // 读取 GPIO8
    ESP_ERROR_CHECK(adc_oneshot_read(turb_ctx->unit_handle, ADC_CHANNEL_7, &adc_raw));
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(turb_ctx->cali_handle, adc_raw, &voltage_mv));
    
    return voltage_mv;
}
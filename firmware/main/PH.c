#include "PH.h"
#include "bsp_adc.h" // 核心：引入公共资源管理器
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "PH";

void ph_init(ph_context_t *ph_ctx)
{
    ESP_LOGI(TAG, "初始化 PH 传感器 (GPIO4)...");

    // 1. 直接向公共管理器索要 ADC1 的句柄，自己搞定！
    ph_ctx->unit_handle = bsp_get_adc1_handle();

    // 2. 配置专属通道 (ADC_CHANNEL_3 对应 GPIO4)
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT, 
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(ph_ctx->unit_handle, ADC_CHANNEL_3, &config));

    // 3. 配置专属校准
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = ADC_CHANNEL_3,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &ph_ctx->cali_handle));
}

int ph_get_voltage_mv(ph_context_t *ph_ctx)
{
    // 每隔 10ms 采一次、共 16 次取平均：
    // 跨越电源纹波周期（~100Hz 纹波周期 10ms），把纹波噪声平均掉
    const int N = 16;
    long sum = 0;
    for (int i = 0; i < N; i++) {
        int adc_raw = 0;
        int voltage_mv = 0;
        if (adc_oneshot_read(ph_ctx->unit_handle, ADC_CHANNEL_3, &adc_raw) == ESP_OK &&
            adc_cali_raw_to_voltage(ph_ctx->cali_handle, adc_raw, &voltage_mv) == ESP_OK) {
            sum += voltage_mv;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return (int)(sum / N);
}
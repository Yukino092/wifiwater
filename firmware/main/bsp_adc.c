#include "bsp_adc.h"
#include "esp_log.h"

static const char *TAG = "BSP_ADC";

// 使用 static 变量，让句柄只在这个文件内存活。初始值为 NULL。
static adc_oneshot_unit_handle_t s_adc1_handle = NULL; 

adc_oneshot_unit_handle_t bsp_get_adc1_handle(void)
{
    // 关键逻辑：如果发现句柄还是空的，说明是第一次调用，那就执行初始化
    if (s_adc1_handle == NULL) {
        ESP_LOGI(TAG, "ADC1 单元首次初始化...");
        adc_oneshot_unit_init_cfg_t init_config = {
            .unit_id = ADC_UNIT_1, 
            .ulp_mode = ADC_ULP_MODE_DISABLE, 
        };
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &s_adc1_handle));
    }
    
    // 如果不是空的（说明之前已经被别的模块初始化过了），就直接返回现成的句柄
    return s_adc1_handle;
}
#include "max98357a.h"
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2s_std.h"

static const char *TAG = "MAX98357A";

// ===== 引脚定义（按你的实际接线调整这三个宏即可）=====
#define I2S_BCLK_IO     15   // BCLK  位时钟
#define I2S_WS_IO       16   // LRCK  左右声道时钟（也叫 WS）
#define I2S_DOUT_IO     17   // DIN   数据输入（接 MAX98357A 的 DIN 脚）

// ===== 音频参数 =====
#define I2S_SAMPLE_RATE 16000                    // 采样率 16kHz（语音够用，可改 44100）
#define I2S_BITS        I2S_DATA_BIT_WIDTH_16BIT // 16bit 采样

static i2s_chan_handle_t tx_chan = NULL; // I2S 发送通道句柄

esp_err_t max98357a_init(void)
{
    // 已经初始化过就跳过，避免重复创建
    if (tx_chan != NULL) {
        return ESP_OK;
    }

    // 1. 创建 I2S 发送通道（主机模式，通道号自动分配）
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &tx_chan, NULL), TAG, "创建 I2S 通道失败");

    // 2. 配置标准 I2S 模式（MAX98357A 用标准 Philips 格式，且无需 MCLK）
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_BITS, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,   // MAX98357A 不需要 MCLK
            .bclk = I2S_BCLK_IO,
            .ws   = I2S_WS_IO,
            .dout = I2S_DOUT_IO,
            .din  = I2S_GPIO_UNUSED,   // 只输出，不采集
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx_chan, &std_cfg), TAG, "初始化 I2S 失败");

    // 3. 使能通道
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx_chan), TAG, "使能 I2S 失败");

    ESP_LOGI(TAG, "MAX98357A 初始化完成: BCLK=%d, WS=%d, DIN=%d, 采样率=%d",
             I2S_BCLK_IO, I2S_WS_IO, I2S_DOUT_IO, I2S_SAMPLE_RATE);
    return ESP_OK;
}

esp_err_t max98357a_write(const int16_t *data, size_t samples)
{
    if (tx_chan == NULL || data == NULL || samples == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t bytes_written = 0;
    // 阻塞式写入，直到所有数据都送到 I2S 外设
    esp_err_t ret = i2s_channel_write(tx_chan, data, samples * sizeof(int16_t),
                                      &bytes_written, portMAX_DELAY);
    return ret;
}

esp_err_t max98357a_test_tone(uint16_t freq_hz, uint16_t duration_ms)
{
    // 生成一段正弦波测试音
    const uint32_t total_samples = (uint32_t)I2S_SAMPLE_RATE * duration_ms / 1000;
    int16_t *buf = malloc(total_samples * sizeof(int16_t));
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }

    const float two_pi = 6.2831853f;
    for (uint32_t i = 0; i < total_samples; i++) {
        float t = (float)i / I2S_SAMPLE_RATE;
        buf[i] = (int16_t)(sinf(two_pi * freq_hz * t) * 24000.0f); // 幅度约 73%，更响亮
    }

    esp_err_t ret = max98357a_write(buf, total_samples);
    free(buf);
    if (ret != ESP_OK) return ret;

    // 追加一段静音（100ms），确保功放彻底停止、不拖尾
    const uint32_t silence_samples = I2S_SAMPLE_RATE / 10;
    int16_t *silence = calloc(silence_samples, sizeof(int16_t));
    if (silence) {
        max98357a_write(silence, silence_samples);
        free(silence);
    }
    return ret;
}

// ===== I2S 引脚顺序自动扫描（找正确接线）=====
typedef struct { int bclk, ws, din; } pin_perm_t;

void max98357a_pin_scan(void)
{
    // 6 种排列组合
    const pin_perm_t perms[6] = {
        {16, 15, 17}, {16, 17, 15},
        {15, 16, 17}, {15, 17, 16},
        {17, 16, 15}, {17, 15, 16},
    };
    // 每种用不同频率，方便区分
    // （改用"数几声"方式识别，不再用不同频率）

    for (int i = 0; i < 6; i++) {
        ESP_LOGI(TAG, "==> 第%d种: BCLK=%d WS=%d DIN=%d  会响 %d 声（听到清晰音的请数几声）",
                 i + 1, perms[i].bclk, perms[i].ws, perms[i].din, i + 1);

        // 销毁旧通道
        if (tx_chan) {
            i2s_channel_disable(tx_chan);
            i2s_del_channel(tx_chan);
            tx_chan = NULL;
        }

        // 用当前组合重建
        i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
        if (i2s_new_channel(&chan_cfg, &tx_chan, NULL) != ESP_OK) continue;

        i2s_std_config_t std_cfg = {
            .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_BITS, I2S_SLOT_MODE_MONO),
            .gpio_cfg = {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = perms[i].bclk,
                .ws   = perms[i].ws,
                .dout = perms[i].din,
                .din  = I2S_GPIO_UNUSED,
                .invert_flags = {0},
            },
        };
        if (i2s_channel_init_std_mode(tx_chan, &std_cfg) != ESP_OK) {
            i2s_del_channel(tx_chan); tx_chan = NULL; continue;
        }
        i2s_channel_enable(tx_chan);

        // 播放 i+1 声短音，通过数几声来识别
        for (int k = 0; k < i + 1; k++) {
            max98357a_test_tone(1000, 200);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        vTaskDelay(pdMS_TO_TICKS(2000)); // 每组之间停 2 秒
    }

    ESP_LOGW(TAG, "==== 扫描结束！请告诉我们：你听到清晰声音的是第几组（1~6，等于滴了几声）====");
}









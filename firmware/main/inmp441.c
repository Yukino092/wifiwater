#include "inmp441.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2s_std.h"

static const char *TAG = "INMP441";

// ===== 引脚定义（⚠️ 请改成你的实际接线）=====
#define I2S_BCLK_IO     13    // BCLK 位时钟
#define I2S_WS_IO       14    // LRCK 左右声道时钟（也叫 WS）
#define I2S_DIN_IO      18    // SD 数据输出（接 ESP32 的数据输入 DIN）

// ===== 音频参数 =====
#define I2S_SAMPLE_RATE 16000                    // 采样率 16kHz（语音够用，可改 44100）
#define I2S_BITS        I2S_DATA_BIT_WIDTH_32BIT // INMP441 用 32bit 槽接收（通用配置）

static i2s_chan_handle_t rx_chan = NULL; // I2S 接收通道句柄

esp_err_t inmp441_init(void)
{
    // 已经初始化过就跳过
    if (rx_chan != NULL) {
        return ESP_OK;
    }

    // 1. 创建 I2S 接收通道（主机模式：ESP32 提供 BCLK 和 WS 时钟，麦克风输出数据）
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &rx_chan), TAG, "创建 I2S 接收通道失败");

    // 2. 配置标准 I2S 模式（INMP441 是立体声格式：左声道数据、右声道空，需 STEREO 读取再取左声道）
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_BITS, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,   // INMP441 不需要 MCLK
            .bclk = I2S_BCLK_IO,
            .ws   = I2S_WS_IO,
            .dout = I2S_GPIO_UNUSED,   // 麦克风只输入，不输出
            .din  = I2S_DIN_IO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx_chan, &std_cfg), TAG, "初始化 I2S 失败");

    // 3. 使能通道
    ESP_RETURN_ON_ERROR(i2s_channel_enable(rx_chan), TAG, "使能 I2S 失败");

    ESP_LOGI(TAG, "INMP441 初始化完成: BCLK=%d, WS=%d, SD=%d, 采样率=%d",
             I2S_BCLK_IO, I2S_WS_IO, I2S_DIN_IO, I2S_SAMPLE_RATE);
    return ESP_OK;
}

// 静态立体声临时缓冲（避免每次 read 都 malloc/free 造成堆碎片和延迟抖动）
#define INMP441_MAX_SAMPLES 960
static int32_t s_stereo_tmp[INMP441_MAX_SAMPLES * 2];

esp_err_t inmp441_read(int32_t *buf, size_t samples)
{
    if (rx_chan == NULL || buf == NULL || samples == 0 || samples > INMP441_MAX_SAMPLES) {
        return ESP_ERR_INVALID_ARG;
    }

    // INMP441 是立体声格式（左声道数据、右声道高阻=0），
    // 读 2 倍采样后只取左声道（偶数下标）
    size_t stereo_samples = samples * 2;

    size_t bytes_read = 0;
    esp_err_t ret = i2s_channel_read(rx_chan, s_stereo_tmp, stereo_samples * sizeof(int32_t),
                                     &bytes_read, portMAX_DELAY);
    if (ret == ESP_OK) {
        for (size_t i = 0; i < samples; i++) {
            buf[i] = s_stereo_tmp[i * 2]; // 取左声道（偶数下标）
        }
    }

    return ret;
}

void inmp441_debug_channels(void)
{
    if (rx_chan == NULL) {
        ESP_LOGE(TAG, "I2S 接收通道未初始化，无法诊断");
        return;
    }
    const int n = 960;
    size_t bytes_read = 0;
    if (i2s_channel_read(rx_chan, s_stereo_tmp, n * 2 * sizeof(int32_t),
                         &bytes_read, portMAX_DELAY) != ESP_OK) {
        ESP_LOGE(TAG, "诊断读取失败");
        return;
    }
    int32_t lpeak = 0, rpeak = 0;
    for (int i = 0; i < n; i++) {
        int32_t l = s_stereo_tmp[i * 2];      // 左声道（偶数下标）
        int32_t r = s_stereo_tmp[i * 2 + 1];  // 右声道（奇数下标）
        if (l < 0) l = -l;
        if (r < 0) r = -r;
        if (l > lpeak) lpeak = l;
        if (r > rpeak) rpeak = r;
    }
    ESP_LOGW(TAG, "麦克风诊断: 左声道原始峰值=%ld 右声道原始峰值=%ld",
             (long)(lpeak >> 16), (long)(rpeak >> 16));
}

int inmp441_read_volume(void)
{
    // 读一小段数据，计算平均绝对值作为音量
    const size_t N = 512;
    int32_t buf[N];

    if (inmp441_read(buf, N) != ESP_OK) {
        return -1;
    }

    int64_t sum = 0;
    for (size_t i = 0; i < N; i++) {
        // 24bit 数据左对齐存储在 32bit 里，右移 8 位得到有符号 24bit 值
        int32_t v = buf[i] >> 8;
        if (v < 0) v = -v;
        sum += v;
    }

    int32_t avg = (int32_t)(sum / N);
    // 映射到 0~100（24bit 最大正值为 2^23-1 = 8388607）
    return (int)(avg * 100 / 8388607);
}

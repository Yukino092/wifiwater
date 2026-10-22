#include "ws2812.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"

static const char *TAG = "WS2812";

// ===== 配置 =====
#define WS2812_PIN            GPIO_NUM_3     // 数据引脚
#define WS2812_LED_NUM        30              // 灯珠数量（⚠️ 按你的灯带实际数量改）
#define WS2812_RESOLUTION_HZ  10000000       // RMT 分辨率 10MHz（1 tick = 0.1us）

static rmt_channel_handle_t led_chan = NULL;
static rmt_encoder_handle_t led_encoder = NULL;
static uint8_t led_buffer[WS2812_LED_NUM * 3]; // 每颗灯 3 字节，GRB 顺序

// ===== 组合编码器：先发 GRB 数据，再发复位信号（长低电平）=====
typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *bytes_encoder; // 数据编码器
    rmt_encoder_t *copy_encoder;  // 复位编码器
    int state;                    // 0=发数据, 1=发复位
    rmt_symbol_word_t reset_code; // 复位码
} ws2812_encoder_t;

static size_t ws2812_encode(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                            const void *primary_data, size_t data_size, rmt_encode_state_t *ret_state)
{
    ws2812_encoder_t *self = (ws2812_encoder_t *)encoder;
    rmt_encode_state_t session_state = 0;
    rmt_encode_state_t state = 0;
    size_t encoded = 0;

    switch (self->state) {
    case 0: // 发送 GRB 数据
        encoded += self->bytes_encoder->encode(self->bytes_encoder, channel,
                                               primary_data, data_size, &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            self->state = 1; // 数据发完，进入复位阶段
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
        // 继续往下走，发复位码
    case 1: // 发送复位信号
        encoded += self->copy_encoder->encode(self->copy_encoder, channel,
                                              &self->reset_code, sizeof(self->reset_code), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            self->state = 0;
            state |= RMT_ENCODING_COMPLETE;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
        }
    }
out:
    *ret_state = state;
    return encoded;
}

static esp_err_t ws2812_encoder_reset(rmt_encoder_t *encoder)
{
    ws2812_encoder_t *self = (ws2812_encoder_t *)encoder;
    rmt_encoder_reset(self->bytes_encoder);
    rmt_encoder_reset(self->copy_encoder);
    self->state = 0;
    return ESP_OK;
}

static esp_err_t ws2812_encoder_del(rmt_encoder_t *encoder)
{
    ws2812_encoder_t *self = (ws2812_encoder_t *)encoder;
    rmt_del_encoder(self->bytes_encoder);
    rmt_del_encoder(self->copy_encoder);
    free(self);
    return ESP_OK;
}

static esp_err_t ws2812_new_encoder(rmt_encoder_handle_t *ret_encoder)
{
    ws2812_encoder_t *self = calloc(1, sizeof(ws2812_encoder_t));
    if (!self) return ESP_ERR_NO_MEM;

    // 字节编码器：把每个 bit 编码成 WS2812 的高低电平波形
    rmt_bytes_encoder_config_t bytes_cfg = {
        .bit0 = { .duration0 = 3, .level0 = 1, .duration1 = 9, .level1 = 0 }, // 0: 0.3us高 + 0.9us低
        .bit1 = { .duration0 = 9, .level0 = 1, .duration1 = 3, .level1 = 0 }, // 1: 0.9us高 + 0.3us低
        .flags.msb_first = 1, // 高位先发
    };
    rmt_new_bytes_encoder(&bytes_cfg, &self->bytes_encoder);

    // 拷贝编码器：用于发送复位信号
    rmt_copy_encoder_config_t copy_cfg = {};
    rmt_new_copy_encoder(&copy_cfg, &self->copy_encoder);

    // 复位码：约 280us + 280us = 560us 低电平（WS2812 要求 >50us）
    self->reset_code = (rmt_symbol_word_t){
        .level0 = 0, .duration0 = 2800,
        .level1 = 0, .duration1 = 2800,
    };

    self->base.encode = ws2812_encode;
    self->base.reset  = ws2812_encoder_reset;
    self->base.del    = ws2812_encoder_del;

    *ret_encoder = &self->base;
    return ESP_OK;
}

static esp_err_t ws2812_flush(void)
{
    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
    };
    return rmt_transmit(led_chan, led_encoder, led_buffer, sizeof(led_buffer), &tx_cfg);
}

esp_err_t ws2812_init(void)
{
    if (led_chan != NULL) return ESP_OK;

    // 1. 创建 RMT 发送通道
    rmt_tx_channel_config_t tx_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = WS2812_PIN,
        .mem_block_symbols = 64,
        .resolution_hz = WS2812_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_cfg, &led_chan), TAG, "创建 RMT 通道失败");

    // 2. 创建 WS2812 编码器
    ESP_RETURN_ON_ERROR(ws2812_new_encoder(&led_encoder), TAG, "创建编码器失败");

    // 3. 使能 RMT 通道
    ESP_RETURN_ON_ERROR(rmt_enable(led_chan), TAG, "使能 RMT 失败");

    // 4. 初始全灭
    memset(led_buffer, 0, sizeof(led_buffer));
    ESP_RETURN_ON_ERROR(ws2812_flush(), TAG, "初始化灯带失败");

    ESP_LOGI(TAG, "WS2812 初始化完成: GPIO=%d, 灯珠数=%d", WS2812_PIN, WS2812_LED_NUM);
    return ESP_OK;
}

esp_err_t ws2812_set_led(uint32_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index >= WS2812_LED_NUM) return ESP_ERR_INVALID_ARG;
    // WS2812 颜色顺序是 GRB（先绿、再红、再蓝）
    led_buffer[index * 3 + 0] = g;
    led_buffer[index * 3 + 1] = r;
    led_buffer[index * 3 + 2] = b;
    return ws2812_flush();
}

esp_err_t ws2812_set_all(uint8_t r, uint8_t g, uint8_t b)
{
    for (uint32_t i = 0; i < WS2812_LED_NUM; i++) {
        led_buffer[i * 3 + 0] = g;
        led_buffer[i * 3 + 1] = r;
        led_buffer[i * 3 + 2] = b;
    }
    return ws2812_flush();
}

esp_err_t ws2812_clear(void)
{
    memset(led_buffer, 0, sizeof(led_buffer));
    return ws2812_flush();
}


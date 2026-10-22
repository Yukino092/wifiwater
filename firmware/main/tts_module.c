#include "tts_module.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_partition.h"
#include "spi_flash_mmap.h"
#include "esp_tts.h"
#include "esp_tts_voice_xiaole.h"
#include "max98357a.h"

static const char *TAG = "TTS";

static esp_tts_handle_t tts = NULL;
static const void *s_voice_data = NULL;
static spi_flash_mmap_handle_t s_mmap_handle = 0;

esp_err_t tts_module_init(void)
{
    if (tts) return ESP_OK;

    // 1. 找语音模型分区
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "voice_data");
    if (!part) {
        ESP_LOGE(TAG, "未找到 voice_data 分区，请先烧录语音模型");
        return ESP_ERR_NOT_FOUND;
    }

    // 2. mmap 分区（模型直接从 flash 读，不占 RAM）
    ESP_RETURN_ON_ERROR(
        esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA,
                           &s_voice_data, &s_mmap_handle),
        TAG, "mmap 语音分区失败");

    // 3. 初始化语音集 + 创建 TTS 实例
    esp_tts_voice_t *voice = esp_tts_voice_set_init(&esp_tts_voice_xiaole, (void *)s_voice_data);
    if (!voice) {
        ESP_LOGE(TAG, "语音集初始化失败");
        return ESP_FAIL;
    }

    tts = esp_tts_create(voice);
    if (!tts) {
        ESP_LOGE(TAG, "TTS 实例创建失败");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "TTS 初始化完成（小乐音色 16kHz）");
    return ESP_OK;
}

esp_err_t tts_module_speak(const char *text)
{
    if (!tts || !text) return ESP_ERR_INVALID_STATE;

    // 1. 中文转音素
    if (esp_tts_parse_chinese(tts, text) != 1) {
        ESP_LOGE(TAG, "中文解析失败: %s", text);
        return ESP_FAIL;
    }

    // 2. 流式取 PCM 并送到功放播放
    int len = 0;
    short *pcm = NULL;
    while ((pcm = esp_tts_stream_play(tts, &len, 2)) != NULL && len > 0) {
        max98357a_write((int16_t *)pcm, (size_t)len);
    }
    esp_tts_stream_reset(tts);

    // 追加一段静音（200ms），清掉功放残留杂音/直流
    const int silence_samples = 3200;
    int16_t *silence = calloc(silence_samples, sizeof(int16_t));
    if (silence) {
        max98357a_write(silence, silence_samples);
        free(silence);
    }

    ESP_LOGI(TAG, "已播报: %s", text);
    return ESP_OK;
}



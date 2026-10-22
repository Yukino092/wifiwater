#ifndef _TTS_MODULE_H_
#define _TTS_MODULE_H_

#include "esp_err.h"

// 初始化 TTS（加载小乐音色语音模型，需先烧录模型到 voice_data 分区）
esp_err_t tts_module_init(void);

// 用中文语音播报一段文本（阻塞，直到播完）
esp_err_t tts_module_speak(const char *text);

#endif // _TTS_MODULE_H_

#ifndef _XIAOZHI_CHAT_H_
#define _XIAOZHI_CHAT_H_

#include "esp_err.h"
#include <stdbool.h>

// 初始化小智云端语音助手（需在 WiFi、max98357a_init、inmp441_init 之后调用）
// 流程：获取设备信息(激活) -> 注册 MCP 设备控制工具 -> 建立云端对话 -> 音频收发
esp_err_t xiaozhi_chat_init(void);

// 让 AI 说话：把 text 当作"用户说的话"发给小智，AI 会用语音回应（会从喇叭出声）。
// 用途：远程测试（无需麦克风即可验证小智连接与喇叭），也可做文字播报。
// 返回 true = 已成功发出（AI 稍后会用语音回应）。
bool xiaozhi_chat_say(const char *text);

// 小智是否已连接（音频通道已打开）
bool xiaozhi_chat_is_ready(void);

#endif // _XIAOZHI_CHAT_H_

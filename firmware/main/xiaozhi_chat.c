#include "xiaozhi_chat.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_event.h"
#include "cJSON.h"

#include "esp_xiaozhi_chat.h"
#include "esp_xiaozhi_info.h"
#include "esp_mcp_engine.h"
#include "esp_mcp_tool.h"
#include "esp_mcp_property.h"
#include "esp_mcp_data.h"

#include "max98357a.h"
#include "inmp441.h"
#include "water_pump.h"
#include "refill_pump.h"
#include "heater.h"
#include "ws2812.h"
#include "esp_audio_types.h"
#include "esp_opus_dec.h"
#include "mqtt_module.h"

// 传感器/设备状态全局值（在 main.c 定义，此处 extern 引用）
extern float g_ph, g_wtemp, g_tds;
extern int g_level1, g_level2;  // 两个数字水位传感器（0=有水 1=无水）
extern bool g_pump_on, g_heater_on, g_refill_on;  // 排水泵/加热棒/补水泵状态
extern bool g_refill_auto;      // 自动补水是否启用
extern int  g_refill_state;     // 补水状态（0=待机 1=补水中 2=已锁定）

// 在 main.c 中实现的统一控制入口（语音与 App 走同一条逻辑，避免状态不同步）
void control_pump(bool on);     // 排水泵：与 PUMP:1/0 一致
void control_refill(bool on);   // 补水泵：与 REFILL:1/0 一致（开会同时启用自动补水）

static const char *TAG = "XZ_CHAT";

#define MIC_CHUNK_SAMPLES 960   // 60ms @16kHz

static esp_xiaozhi_chat_handle_t s_chat = 0;
static volatile bool s_audio_open = false;
static volatile bool s_speaking = false;          // 小智正在说话（TTS 播放中）
static volatile TickType_t s_speak_stop_ticks = 0; // TTS 结束时刻，之后短暂静默避免回声误触发
static void *s_opus_dec = NULL;  // OPUS 解码器（服务器 TTS 下行是 OPUS）

// ===== 麦克风诊断（供 MQTT 上报，用于远程判断麦克风到底有没有在工作）=====
// 客户反馈"跟 AI 说话没反应、后台也没有对话记录"时，靠这两个值就能定性：
//   客户说话时 麦克风峰值 会跳到几百~几千  → 麦克风正常，问题在小智会话/网络
//   客户说话时 麦克风峰值 一直是 0~50      → 麦克风没在工作（接线/供电/通道），云端根本收不到声音
int  g_mic_peak = 0;             // 最近一次读到的原始峰值（16bit）
bool g_mic_audio_open = false;   // 小智音频通道是否已打开（1=打开）
static TickType_t s_mic_diag_ticks = 0;  // 空闲时的诊断采样节流

#define MAX_OPUS_PACKET 512   // OPUS 60ms@16k 帧实测 ~200-250 字节，512 足够；省堆内存给 TLS
typedef struct {
    int len;
    uint8_t data[MAX_OPUS_PACKET];
} opus_packet_t;
static QueueHandle_t s_opus_queue = NULL;  // OPUS 数据队列（WebSocket 回调 -> 解码任务）

typedef struct {
    int16_t data[MIC_CHUNK_SAMPLES];  // 960 采样 = 60ms PCM
} mic_chunk_t;
static QueueHandle_t s_mic_queue = NULL;  // 麦克风 PCM 队列（采集任务 -> 发送任务）

// ==================== MCP 设备控制工具回调 ====================

static esp_mcp_value_t tool_get_water_quality(const esp_mcp_property_list_t *props)
{
    (void)props;
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return esp_mcp_value_create_bool(false);
    }
    cJSON_AddNumberToObject(root, "ph", g_ph);
    cJSON_AddNumberToObject(root, "water_temp", g_wtemp);
    cJSON_AddNumberToObject(root, "tds", g_tds);
    // 水位用文字描述，方便 AI 直接念给用户（0=有水 1=无水）
    cJSON_AddStringToObject(root, "low_water_level", g_level1 ? "无水" : "有水");
    cJSON_AddStringToObject(root, "high_water_level", g_level2 ? "无水" : "有水");
    // 设备当前状态
    cJSON_AddStringToObject(root, "drain_pump", g_pump_on ? "开" : "关");
    cJSON_AddStringToObject(root, "refill_pump", g_refill_on ? "开" : "关");
    cJSON_AddStringToObject(root, "refill_auto", g_refill_auto ? "已启用" : "已停用");
    const char *rs = "待机";
    if (!g_refill_auto)            rs = "已停用";
    else if (g_refill_state == 1)  rs = "补水中";
    cJSON_AddStringToObject(root, "refill_state", rs);
    cJSON_AddStringToObject(root, "heater", g_heater_on ? "开" : "关");

    ESP_LOGI(TAG, "语音控制: 读取状态 pH=%.2f 水温=%.1f TDS=%.0f 低水位=%s 高水位=%s 排水=%s 补水=%s(%s) 加热=%s",
             g_ph, g_wtemp, g_tds,
             g_level1 ? "无水" : "有水", g_level2 ? "无水" : "有水",
             g_pump_on ? "开" : "关", g_refill_on ? "开" : "关", rs, g_heater_on ? "开" : "关");
    char *js = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!js) {
        return esp_mcp_value_create_bool(false);
    }
    esp_mcp_value_t v = esp_mcp_value_create_string(js);
    free(js);
    return v;
}

static esp_mcp_value_t tool_pump_set(const esp_mcp_property_list_t *props)
{
    int on = esp_mcp_property_list_get_property_int(props, "on");
    // 走 main.c 的统一入口，与 App 的 PUMP:1/0 行为完全一致
    // （以前直接操作 GPIO，绕过了状态机，会出现"刚开就被自动逻辑关掉"）
    control_pump(on ? true : false);
    ESP_LOGI(TAG, "语音控制: 排水泵 %s", on ? "开" : "关");
    return esp_mcp_value_create_bool(true);
}

static esp_mcp_value_t tool_refill_set(const esp_mcp_property_list_t *props)
{
    int on = esp_mcp_property_list_get_property_int(props, "on");
    // 走 main.c 的统一入口：开会同时启用自动补水，并进入手动优先期（10 分钟内自动逻辑不干预）
    control_refill(on ? true : false);
    ESP_LOGI(TAG, "语音控制: 补水泵 %s", on ? "开" : "关");
    return esp_mcp_value_create_bool(true);
}

static esp_mcp_value_t tool_heater_set(const esp_mcp_property_list_t *props)
{
    int on = esp_mcp_property_list_get_property_int(props, "on");
    heater_set_state(on ? 1 : 0);
    ESP_LOGI(TAG, "语音控制: 加热棒 %s", on ? "开" : "关");
    return esp_mcp_value_create_bool(true);
}

static esp_mcp_value_t tool_led_set(const esp_mcp_property_list_t *props)
{
    int on = esp_mcp_property_list_get_property_int(props, "on");
    if (on) {
        ws2812_set_all(255, 255, 255);
    } else {
        ws2812_clear();
    }
    ESP_LOGI(TAG, "语音控制: 灯带 %s", on ? "开" : "关");
    return esp_mcp_value_create_bool(true);
}

static esp_mcp_value_t tool_led_rgb(const esp_mcp_property_list_t *props)
{
    int r = esp_mcp_property_list_get_property_int(props, "red");
    int g = esp_mcp_property_list_get_property_int(props, "green");
    int b = esp_mcp_property_list_get_property_int(props, "blue");
    ws2812_set_all(r, g, b);
    ESP_LOGI(TAG, "语音控制: 灯带颜色 RGB(%d,%d,%d)", r, g, b);
    return esp_mcp_value_create_bool(true);
}

// ==================== 音频回调（服务器 TTS 音频 -> 扬声器）====================

static void audio_cb(const uint8_t *data, int len, void *ctx)
{
    (void)ctx;
    if (!s_opus_queue || !data || len <= 0 || len > MAX_OPUS_PACKET) {
        return;
    }
    opus_packet_t pkt;
    pkt.len = len;
    memcpy(pkt.data, data, len);
    if (xQueueSend(s_opus_queue, &pkt, 0) != pdTRUE) {
        // 队列满丢帧会导致爆音/卡顿，计数用于诊断
        static uint32_t drop_cnt = 0;
        drop_cnt++;
        if (drop_cnt <= 5 || drop_cnt % 100 == 0) {
            ESP_LOGW(TAG, "音频队列满，丢帧 (累计 %u)", (unsigned)drop_cnt);
        }
    }
}

// OPUS 解码任务：在独立大栈任务里解码（libopus SILK 解码器栈占用大，
// 不能跑在 WebSocket 回调的小栈里，否则栈溢出崩溃）
static void opus_decode_task(void *arg)
{
    (void)arg;
    static uint8_t pcm[4096];         // 静态缓冲，不占任务栈
    static int16_t silence[320] = {0}; // 20ms 静音，队列空时持续喂给 I2S 防止 DMA 欠载
    opus_packet_t pkt;
    int dbg = 0;
    while (1) {
        if (xQueueReceive(s_opus_queue, &pkt, pdMS_TO_TICKS(20)) != pdTRUE) {
            // 队列空（句间停顿/网络抖动）：喂静音，避免 I2S DMA 欠载产生爆音/杂音
            max98357a_write(silence, 320);
            continue;
        }
        esp_audio_dec_in_raw_t raw = {
            .buffer = pkt.data,
            .len = (uint32_t)pkt.len,
            .consumed = 0,
            .frame_recover = ESP_AUDIO_DEC_RECOVERY_NONE,
        };
        esp_audio_dec_out_frame_t frame = {
            .buffer = pcm,
            .len = sizeof(pcm),
            .needed_size = 0,
            .decoded_size = 0,
        };
        esp_audio_dec_info_t info = {0};
        // 循环解码：一个消息可能含多帧 OPUS
        while (raw.len > 0) {
            frame.decoded_size = 0;
            frame.needed_size = 0;
            esp_audio_err_t ret = esp_opus_dec_decode(s_opus_dec, &raw, &frame, &info);
            if (ret == ESP_AUDIO_ERR_OK && frame.decoded_size > 0) {
                if (dbg++ < 5) {
                    ESP_LOGI(TAG, "OPUS解码: 包长=%d 已消费=%d 输出=%d字节 采样率=%d 声道=%d",
                             pkt.len, (int)raw.consumed, (int)frame.decoded_size,
                             (int)info.sample_rate, (int)info.channel);
                }
                max98357a_write((const int16_t *)frame.buffer, frame.decoded_size / 2);
            } else if (ret != ESP_AUDIO_ERR_OK) {
                ESP_LOGW(TAG, "OPUS 解码失败: %d (len=%d)", (int)ret, pkt.len);
                break;
            }
            if (raw.consumed == 0) {
                break;  // 避免死循环
            }
            raw.buffer += raw.consumed;
            raw.len -= raw.consumed;
        }
    }
}

// ==================== 聊天事件回调 ====================

static void event_cb(esp_xiaozhi_chat_event_t event, void *event_data, void *ctx)
{
    (void)ctx;
    switch (event) {
    case ESP_XIAOZHI_CHAT_EVENT_CHAT_TEXT: {
        esp_xiaozhi_chat_text_data_t *t = (esp_xiaozhi_chat_text_data_t *)event_data;
        if (t && t->text) {
            ESP_LOGI(TAG, "[%s] %s",
                     t->role == ESP_XIAOZHI_CHAT_TEXT_ROLE_USER ? "我说" : "小智", t->text);
        }
        break;
    }
    case ESP_XIAOZHI_CHAT_EVENT_CHAT_TTS_STATE: {
        esp_xiaozhi_chat_tts_state_t *s = (esp_xiaozhi_chat_tts_state_t *)event_data;
        if (s) {
            if (s->state == ESP_XIAOZHI_CHAT_TTS_STATE_START) {
                ESP_LOGI(TAG, "小智开始说话");
                s_speaking = true;
            } else if (s->state == ESP_XIAOZHI_CHAT_TTS_STATE_STOP) {
                ESP_LOGI(TAG, "小智说完");
                s_speaking = false;
                s_speak_stop_ticks = xTaskGetTickCount();
            } else if (s->state == ESP_XIAOZHI_CHAT_TTS_STATE_SENTENCE_START && s->text) {
                ESP_LOGI(TAG, "句子: %s", s->text);
            }
        }
        break;
    }
    case ESP_XIAOZHI_CHAT_EVENT_CHAT_ERROR: {
        esp_xiaozhi_chat_error_info_t *e = (esp_xiaozhi_chat_error_info_t *)event_data;
        if (e) {
            ESP_LOGE(TAG, "聊天错误: %s (来源: %s)",
                     esp_err_to_name(e->code), e->source ? e->source : "");
            // 上报到 MQTT：远程也能看到小智出错（MQTT 与语音是两条独立链路）
            char errbuf[80];
            snprintf(errbuf, sizeof(errbuf), "[XIAOZHI] 错误:%s", esp_err_to_name(e->code));
            mqtt_module_send(errbuf);
        }
        break;
    }
    case ESP_XIAOZHI_CHAT_EVENT_CHAT_SYSTEM_CMD:
        ESP_LOGI(TAG, "系统命令: %s", (const char *)event_data);
        break;
    default:
        break;
    }
}

// ==================== 连接事件 ====================

static void conn_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    switch (id) {
    case ESP_XIAOZHI_CHAT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "已连接小智服务器");
        mqtt_module_send("[XIAOZHI] 已连接服务器");
        if (s_chat) {
            esp_xiaozhi_chat_audio_t audio = {
                .format = "pcm",
                .sample_rate = 16000,
                .channels = 1,
                .frame_duration = 60,
            };
            esp_err_t r = esp_xiaozhi_chat_open_audio_channel(s_chat, &audio, NULL, 0);
            ESP_LOGI(TAG, "打开音频通道: %s", esp_err_to_name(r));
            if (r == ESP_OK) {
                // 0 = 实时模式：云端持续流式识别，检测更可靠（不易漏听）；
                // 误触发已由半双工 + 噪声门压制，故用实时模式换取识别稳定性。
                esp_xiaozhi_chat_send_start_listening(s_chat, ESP_XIAOZHI_CHAT_LISTENING_MODE_REALTIME);
                s_audio_open = true;
            }
        }
        break;
    case ESP_XIAOZHI_CHAT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "与小智服务器断开");
        // 带上剩余内存：小智连不上最常见的原因就是 TLS 握手内存不足
        // （历史上出现过 mbedtls_ssl_setup -0x7F00 = 分配失败）。
        // 远程看到「已断开」反复刷屏时，看这个数字就能判断是不是内存问题。
        {
            char dbuf[128];
            snprintf(dbuf, sizeof(dbuf), "[XIAOZHI] 已断开 剩余内存=%uKB 最小历史=%uKB",
                     (unsigned)(esp_get_free_heap_size() / 1024),
                     (unsigned)(esp_get_minimum_free_heap_size() / 1024));
            mqtt_module_send(dbuf);
        }
        s_audio_open = false;
        break;
    case ESP_XIAOZHI_CHAT_EVENT_AUDIO_CHANNEL_OPENED:
        ESP_LOGI(TAG, "音频通道已打开");
        break;
    case ESP_XIAOZHI_CHAT_EVENT_AUDIO_CHANNEL_CLOSED:
        ESP_LOGI(TAG, "音频通道已关闭");
        s_audio_open = false;
        break;
    default:
        break;
    }
}

// ==================== 麦克风采集 / 发送任务 ====================

// 采集任务（高优先级）：只从 I2S 连续取麦克风数据并压入队列，不与网络发送耦合，
// 保证网络卡顿时采集不断档（否则云端 ASR 会漏音节、识别变差）。
static void mic_task(void *arg)
{
    (void)arg;
    int32_t *raw = malloc(MIC_CHUNK_SAMPLES * sizeof(int32_t));
    static mic_chunk_t chunk;  // 静态缓冲，不占任务栈（1920 字节较大）
    if (!raw) {
        ESP_LOGE(TAG, "麦克风缓冲分配失败");
        vTaskDelete(NULL);
        return;
    }
    while (1) {
        g_mic_audio_open = s_audio_open;   // 诊断用：音频通道状态
        // 半双工：小智说话时 / 说完后 60ms 内不采麦克风，
        // 避免扬声器回声被云端 VAD 当成用户说话造成误触发（60ms 够消回声、又能更快接住你的下一句话）。
        bool grace = ((xTaskGetTickCount() - s_speak_stop_ticks) < pdMS_TO_TICKS(60));
        if (s_audio_open && !s_speaking && !grace) {
            if (inmp441_read(raw, MIC_CHUNK_SAMPLES) == ESP_OK) {
                // 24bit 左对齐 -> 16bit，同时统计原始峰值
                int16_t raw_peak = 0;
                for (int i = 0; i < MIC_CHUNK_SAMPLES; i++) {
                    chunk.data[i] = (int16_t)(raw[i] >> 16);
                    int a = chunk.data[i] < 0 ? -chunk.data[i] : chunk.data[i];
                    if (a > raw_peak) raw_peak = a;
                }
                g_mic_peak = raw_peak;   // 诊断用：上报给 App/MQTT
                // 噪声门 + 人声增益（保持连续上行流，云端 VAD/ASR 需要连续音频）：
                // - 峰值 < 500 判为环境噪声（实测底噪约 240~400），压到 1/2；
                // - 峰值 >= 500 判为人声，放大到约 3/4 量程（最多 96 倍），小声说话也能听清。
                if (raw_peak < 500) {
                    for (int i = 0; i < MIC_CHUNK_SAMPLES; i++) {
                        chunk.data[i] >>= 1;  // ÷2
                    }
                    static int dbg_noise = 0;
                    if (dbg_noise++ < 3) {
                        ESP_LOGI(TAG, "噪声门: 峰值=%d 已压低", raw_peak);
                    }
                } else {
                    float gain = 24576.0f / raw_peak;
                    if (gain > 96.0f) gain = 96.0f;
                    for (int i = 0; i < MIC_CHUNK_SAMPLES; i++) {
                        int32_t v = (int32_t)(chunk.data[i] * gain);
                        if (v > 32767) v = 32767;
                        if (v < -32768) v = -32768;
                        chunk.data[i] = (int16_t)v;
                    }
                    static int dbg_speech = 0;
                    if (dbg_speech++ < 15) {
                        ESP_LOGI(TAG, "人声: 峰值=%d 增益=%.1fx", raw_peak, gain);
                    }
                }
                // 非阻塞入队；队满说明网络太慢，丢最新一段保持实时、不乱序
                if (s_mic_queue) {
                    if (xQueueSend(s_mic_queue, &chunk, 0) != pdTRUE) {
                        static uint32_t mic_drop = 0;
                        mic_drop++;
                        if (mic_drop <= 5 || mic_drop % 50 == 0) {
                            ESP_LOGW(TAG, "麦克风队列满丢音频 (累计 %u)", (unsigned)mic_drop);
                        }
                    }
                }
            }
        } else {
            // 未到发送时机（音频通道未开 / 小智正在说话 / 回声保护期）：
            // 仍然每 500ms 读一次麦克风【仅用于诊断峰值】，不发送，所以不会造成回声。
            // 这样即使小智没连上，也能从上报里看出"麦克风到底有没有在工作"。
            if ((xTaskGetTickCount() - s_mic_diag_ticks) > pdMS_TO_TICKS(500)) {
                s_mic_diag_ticks = xTaskGetTickCount();
                if (inmp441_read(raw, MIC_CHUNK_SAMPLES) == ESP_OK) {
                    int16_t pk = 0;
                    for (int i = 0; i < MIC_CHUNK_SAMPLES; i++) {
                        int32_t v = raw[i] >> 16;
                        int a = v < 0 ? (int)(-v) : (int)v;
                        if (a > pk) pk = (int16_t)a;
                    }
                    g_mic_peak = pk;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

// 发送任务：从队列取 PCM 发给云端。网络阻塞只影响发送、不影响上面的连续采集。
static void mic_send_task(void *arg)
{
    (void)arg;
    static mic_chunk_t chunk;  // 静态缓冲，不占任务栈
    while (1) {
        if (xQueueReceive(s_mic_queue, &chunk, portMAX_DELAY) == pdTRUE && s_chat) {
            esp_xiaozhi_chat_send_audio_data(s_chat, (const char *)chunk.data,
                                             MIC_CHUNK_SAMPLES * 2);
        }
    }
}

// ==================== 初始化 ====================

esp_err_t xiaozhi_chat_init(void)
{
    esp_err_t ret = ESP_OK;
    esp_mcp_t *mcp = NULL;

    // 1. 获取设备信息，等待激活（激活码通过串口打印，用户在 xiaozhi.me 绑定）
    esp_xiaozhi_chat_info_t info = {0};
    while (1) {
        esp_xiaozhi_chat_free_info(&info);
        memset(&info, 0, sizeof(info));
        ret = esp_xiaozhi_chat_get_info(&info);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "获取设备信息失败: %s，10 秒后重试", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }
        if (info.has_activation_code) {
            ESP_LOGW(TAG, "==============================================");
            ESP_LOGW(TAG, "请打开 https://xiaozhi.me 绑定激活码:");
            ESP_LOGW(TAG, "  激活码: %s", info.activation_code);
            if (info.activation_message) {
                ESP_LOGW(TAG, "  提示: %s", info.activation_message);
            }
            ESP_LOGW(TAG, "==============================================");
            char actbuf[96];
            snprintf(actbuf, sizeof(actbuf), "[XIAOZHI] 未激活，激活码:%s", info.activation_code);
            mqtt_module_send(actbuf);
        }
        // 只有「无激活码 + 有传输配置」才算真正绑定成功，否则继续轮询等待绑定
        if (!info.has_activation_code &&
            (info.has_mqtt_config || info.has_websocket_config)) {
            ESP_LOGI(TAG, "设备已激活 (mqtt=%d, websocket=%d)",
                     info.has_mqtt_config, info.has_websocket_config);
            mqtt_module_send("[XIAOZHI] 已激活");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    // 2. 创建 MCP 引擎并注册水族设备控制工具
    ret = esp_mcp_create(&mcp);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建 MCP 引擎失败: %s", esp_err_to_name(ret));
        goto fail;
    }

    esp_mcp_tool_t *tool = NULL;
    esp_mcp_property_t *prop = NULL;

    tool = esp_mcp_tool_create("self.get_water_quality",
                               "读取鱼缸完整状态（pH/水温/TDS/水位/排水泵/补水泵/加热棒）",
                               tool_get_water_quality);
    esp_mcp_add_tool(mcp, tool);

    tool = esp_mcp_tool_create("self.water_pump.set",
                               "控制排水泵（开=排水，关=停止排水）",
                               tool_pump_set);
    prop = esp_mcp_property_create_with_range("on", 0, 1);
    esp_mcp_tool_add_property(tool, prop);
    esp_mcp_add_tool(mcp, tool);

    tool = esp_mcp_tool_create("self.refill_pump.set",
                               "控制补水泵（开=往鱼缸补水，补到高水位自动停）",
                               tool_refill_set);
    prop = esp_mcp_property_create_with_range("on", 0, 1);
    esp_mcp_tool_add_property(tool, prop);
    esp_mcp_add_tool(mcp, tool);

    tool = esp_mcp_tool_create("self.heater.set", "开关加热棒", tool_heater_set);
    prop = esp_mcp_property_create_with_range("on", 0, 1);
    esp_mcp_tool_add_property(tool, prop);
    esp_mcp_add_tool(mcp, tool);

    tool = esp_mcp_tool_create("self.led.set", "开关灯带", tool_led_set);
    prop = esp_mcp_property_create_with_range("on", 0, 1);
    esp_mcp_tool_add_property(tool, prop);
    esp_mcp_add_tool(mcp, tool);

    tool = esp_mcp_tool_create("self.led.set_rgb",
                               "设置灯带颜色(red/green/blue 取值 0-255)",
                               tool_led_rgb);
    prop = esp_mcp_property_create_with_range("red", 0, 255);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_range("green", 0, 255);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_range("blue", 0, 255);
    esp_mcp_tool_add_property(tool, prop);
    esp_mcp_add_tool(mcp, tool);

    // 3. 初始化并启动聊天会话
    esp_xiaozhi_chat_config_t cfg = ESP_XIAOZHI_CHAT_DEFAULT_CONFIG();
    cfg.audio_callback = audio_cb;
    cfg.event_callback = event_cb;
    cfg.mcp_engine = mcp;
    cfg.owns_mcp_engine = true;
    // 强制使用 WebSocket（MQTT+UDP 连接被服务器拒绝：TLS 握手后 EOF）
    cfg.has_mqtt_config = false;
    cfg.has_websocket_config = info.has_websocket_config;
    mcp = NULL; // 所有权交给 chat

    ret = esp_xiaozhi_chat_init(&cfg, &s_chat);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化聊天失败: %s", esp_err_to_name(ret));
        goto fail;
    }

    esp_event_handler_register(ESP_XIAOZHI_CHAT_EVENTS, ESP_EVENT_ANY_ID, conn_event, NULL);

    ret = esp_xiaozhi_chat_start(s_chat);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "启动聊天失败: %s", esp_err_to_name(ret));
        goto fail;
    }

    // 4. 打开 OPUS 解码器（服务器 TTS 下行是 OPUS 格式）
    esp_opus_dec_cfg_t opus_cfg = {
        .sample_rate = ESP_AUDIO_SAMPLE_RATE_16K,
        .channel = ESP_AUDIO_MONO,
        .frame_duration = ESP_OPUS_DEC_FRAME_DURATION_60_MS,
        .self_delimited = false,
    };
    if (esp_opus_dec_open(&opus_cfg, sizeof(opus_cfg), &s_opus_dec) != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "OPUS 解码器打开失败");
        goto fail;
    }

    // 4.5 创建音频队列 + 独立解码任务（大栈）
    s_opus_queue = xQueueCreate(32, sizeof(opus_packet_t));
    if (!s_opus_queue) {
        ESP_LOGE(TAG, "创建音频队列失败");
        goto fail;
    }
    xTaskCreate(opus_decode_task, "xz_opus_dec", 24576, NULL, 5, NULL);

    // 5. 麦克风采集任务（高优先级保证连续采集）+ 独立发送任务（解耦网络）
    s_mic_queue = xQueueCreate(8, sizeof(mic_chunk_t));
    if (!s_mic_queue) {
        ESP_LOGE(TAG, "创建麦克风队列失败");
        goto fail;
    }
    xTaskCreate(mic_task, "xz_mic", 6144, NULL, 10, NULL);
    xTaskCreate(mic_send_task, "xz_mic_send", 8192, NULL, 5, NULL);

    esp_xiaozhi_chat_free_info(&info);
    ESP_LOGI(TAG, "小智语音助手已启动");
    mqtt_module_send("[XIAOZHI] 已启动");
    return ESP_OK;

fail:
    if (mcp) {
        esp_mcp_destroy(mcp);
    }
    esp_xiaozhi_chat_free_info(&info);
    return ret;
}

// ==================== 远程「让 AI 说话」 ====================
// 原理：小智协议里 type=listen / state=detect 携带 text 表示"用户说了这句话"，
//       服务器会像收到语音一样处理，并用 TTS 语音回应。
//       esp_xiaozhi_chat_send_wake_word() 内部发的正是这条消息，因此可以直接复用。
// 用途：不用麦克风也能远程验证「小智连接是否正常」和「喇叭是否能出声」。
bool xiaozhi_chat_say(const char *text)
{
    if (!s_chat || !text || text[0] == '\0') {
        ESP_LOGW(TAG, "发送文本失败：参数无效");
        return false;
    }
    if (!s_audio_open) {
        ESP_LOGW(TAG, "发送文本失败：小智音频通道未打开（可能还没连上服务器）");
        return false;
    }
    esp_err_t r = esp_xiaozhi_chat_send_wake_word(s_chat, text);
    ESP_LOGI(TAG, "已发送文本给小智: \"%s\" -> %s", text, esp_err_to_name(r));
    return (r == ESP_OK);
}

bool xiaozhi_chat_is_ready(void)
{
    return (s_chat != 0) && s_audio_open;
}

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs.h"
#include "nvs_flash.h"

// 本地配置（WiFi / MQTT / 设备 MAC）：放在 secrets.h，不进仓库
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

// 通信
#include "wifi_module.h"
#include "mqtt_module.h"
#include "ota_module.h"

// 传感器
#include "PH.h"
#include "TDS.h"
#include "water_temp.h"
#include "water_level.h"

// 执行器
#include "ws2812.h"
#include "buzzer.h"
#include "heater.h"
#include "water_pump.h"
#include "refill_pump.h"

// 音频
#include "max98357a.h"
#include "tts_module.h"
#include "inmp441.h"
#include "xiaozhi_chat.h"

static const char *TAG = "MAIN";

// ADC 传感器上下文
static ph_context_t ph_ctx;
static tds_context_t tds_ctx;

// 最新传感器值（供语音播报 + 小智 MCP 工具读取）
float g_ph = 0, g_wtemp = 0, g_tds = 0;
int g_level1 = 0, g_level2 = 0;  // 两个数字水位传感器（1=高 0=低）

// 麦克风诊断值（在 xiaozhi_chat.c 定义，这里引用后随上报发给 App/MQTT）
extern int  g_mic_peak;
extern bool g_mic_audio_open;

// ===== pH 软件校准（存 NVS，断电不丢）=====
static float g_ph_v7 = 0;       // 两点校准：pH 7.0 缓冲液电压(mV)
static float g_ph_v4 = 0;       // 两点校准：pH 4.0 缓冲液电压(mV)
static float g_ph_ref_ph = 0;   // 单点校准：参考 pH 值
static float g_ph_ref_mv = 0;   // 单点校准：参考电压(mV)
#define PH_CAL_NS "phcal"

static void ph_cal_load(void)
{
    nvs_handle_t h;
    if (nvs_open(PH_CAL_NS, NVS_READONLY, &h) == ESP_OK) {
        int32_t v7 = 0, v4 = 0, refph = 0, refmv = 0;
        if (nvs_get_i32(h, "v7", &v7) == ESP_OK) g_ph_v7 = (float)v7;
        if (nvs_get_i32(h, "v4", &v4) == ESP_OK) g_ph_v4 = (float)v4;
        if (nvs_get_i32(h, "refph", &refph) == ESP_OK) g_ph_ref_ph = refph / 100.0f;
        if (nvs_get_i32(h, "refmv", &refmv) == ESP_OK) g_ph_ref_mv = (float)refmv;
        nvs_close(h);
    }
    if (g_ph_v7 > 0 && g_ph_v4 > 0) {
        ESP_LOGI(TAG, "已加载 pH 两点校准: V7=%.0fmV V4=%.0fmV", g_ph_v7, g_ph_v4);
    } else if (g_ph_ref_ph > 0) {
        ESP_LOGI(TAG, "已加载 pH 单点校准: pH %.2f -> %.0fmV", g_ph_ref_ph, g_ph_ref_mv);
    }
}

static void ph_cal_save(void)
{
    nvs_handle_t h;
    if (nvs_open(PH_CAL_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, "v7", (int32_t)g_ph_v7);
        nvs_set_i32(h, "v4", (int32_t)g_ph_v4);
        nvs_set_i32(h, "refph", (int32_t)(g_ph_ref_ph * 100.0f));
        nvs_set_i32(h, "refmv", (int32_t)g_ph_ref_mv);
        nvs_commit(h);
        nvs_close(h);
    }
}

// 用校准值计算 pH；优先级：两点校准 > 单点校准 > 默认近似
static float ph_calc(float mv)
{
    // 1. 两点校准（最准）
    if (g_ph_v7 > 0 && g_ph_v4 > 0 && g_ph_v7 != g_ph_v4) {
        float slope = (g_ph_v7 - g_ph_v4) / 3.0f; // mV/pH
        return 7.0f + (g_ph_v7 - mv) / slope;
    }
    // 2. 单点校准（斜率用默认 180mV/pH，只修正零点）
    if (g_ph_ref_ph > 0 && g_ph_ref_mv > 0) {
        return g_ph_ref_ph + (g_ph_ref_mv - mv) / 180.0f;
    }
    // 3. 默认近似
    return 7.0f + (2500.0f - mv) / 180.0f;
}

// ===== 伪造 pH 显示：限制在参考点 ±0.4 内（用户要求 7.2±0.4）=====
// pH 探头电压漂移时，读数会飙到十几甚至更高。这里把计算出的 pH 夹在
// [参考pH - 0.4, 参考pH + 0.4] 之间，保证显示永远在 7.2±0.4 附近。
static float ph_fake_clamp(float ph)
{
    float center = (g_ph_ref_ph > 0) ? g_ph_ref_ph : 7.2f;  // 有单点校准则以其为基准
    float lo = center - 0.4f;
    float hi = center + 0.4f;
    if (ph < lo) ph = lo;
    if (ph > hi) ph = hi;
    return ph;
}

// pH 指数平滑滤波（EMA），进一步抑制跳动
static float g_ph_smooth = 0;
static bool g_ph_smooth_init = false;

// ===== 中文数字转语音辅助 =====
static const char *CN_D[] = {"零","一","二","三","四","五","六","七","八","九"};

// 整数转中文（0~9999）
static void int_to_cn(int num, char *out) {
    if (num <= 0) { strcpy(out, "零"); return; }
    if (num >= 10000) num = 9999;
    out[0] = '\0';
    int q = num / 1000;
    int b = (num % 1000) / 100;
    int s = (num % 100) / 10;
    int g = num % 10;
    if (q) { strcat(out, CN_D[q]); strcat(out, "千"); }
    if (b) { strcat(out, CN_D[b]); strcat(out, "百"); }
    else if (q && (s || g)) strcat(out, "零");
    if (s) {
        if (s != 1 || q || b) strcat(out, CN_D[s]);
        strcat(out, "十");
    } else if ((q || b) && g) strcat(out, "零");
    if (g) strcat(out, CN_D[g]);
}

// 浮点数转中文（一位小数，如 7.2 -> 七点二）
static void float_to_cn(float val, char *out) {
    int ip = (int)val;
    int dp = (int)((val - ip) * 10 + 0.5f);
    if (dp < 0) dp = 0;
    if (dp > 9) dp = 9;
    int_to_cn(ip, out);
    strcat(out, "点");
    strcat(out, CN_D[dp]);
}

// 组装播报句子
static void build_announcement(char *buf, size_t len) {
    char ph_s[32], temp_s[32], tds_s[32];
    float_to_cn(g_ph, ph_s);
    int_to_cn((int)(g_wtemp + 0.5f), temp_s);
    int_to_cn((int)(g_tds + 0.5f), tds_s);
    snprintf(buf, len,
             "水质报告，酸碱度%s，水温%s度，溶解固体%s，低水位%s，高水位%s",
             ph_s, temp_s, tds_s,
             g_level1 ? "无水" : "有水", g_level2 ? "无水" : "有水");
}

// ===== 自动调节阈值（按需调整）=====
#define TEMP_LOW   20.0f   // 水温低于此值 -> 报警并加热
#define TEMP_HIGH  25.0f   // 水温高于此值 -> 停止加热（恢复正常）

// ===== 水泵超时保护（防传感器故障导致无限抽水/补水）=====
#define PUMP_MAX_RUN_MS       300000   // 任意泵单次连续运行上限：5 分钟
#define REFILL_BOOT_GRACE_MS  15000    // 上电后 15 秒内不自动补水（先让水位读到稳定值）

bool g_heater_on = false; // 加热器当前状态（供小智语音助手读取）
bool g_pump_on = false;   // 排水泵当前状态（供小智语音助手读取）
bool g_refill_on = false; // 补水泵当前状态（供小智语音助手读取）

static TickType_t s_drain_start = 0;    // 排水泵本次启动时刻
static TickType_t s_refill_start = 0;   // 补水泵本次启动时刻
static TickType_t s_boot_ticks = 0;     // 上电时刻

// ============================================================
//  根据水位的自动补水程序
// ------------------------------------------------------------
//  水位传感器约定（NPN 输出，内部上拉）：
//     1 号 GPIO9  = 低水位：0=有水(浸没)  1=无水(露出)
//     2 号 GPIO38 = 高水位：0=有水(浸没)  1=无水(露出)
//
//  补水策略（两段式 + 迟滞，把水补到最高点）：
//     · 低水位「无水」 → 开始补水
//     · 高水位「有水」 → 停止补水（水位已到最高点）
//     · 水位在两个探头之间 → 保持现状（避免临界点反复启停）
//
//  安全保护与恢复方式（尽量不用人工干预，也没有"锁定"状态）：
//     1) 上电 15 秒宽限：等水位读数稳定，避免一上电就开始补水
//     2) 单次补水最长 5 分钟：超时强制停泵，并自动停用自动补水
//        → 恢复：发一条 REFILLAUTO:1（App 上点「自动补水:开」，
//          或告警横幅上的「一键恢复自动补水」）
//     3) 读数矛盾（低水位无水 + 高水位有水，物理上不可能）：
//        停泵 + 告警，但【不锁定】——读数恢复正常后自动继续，无需人工
//     4) 手动 REFILL:0 停用自动补水，保证「关得掉」
//
//  指令：
//     REFILL:1      手动开泵 + 启用自动补水
//     REFILL:0      手动关泵 + 停用自动补水
//     REFILLAUTO:1  只启用自动补水（不动水泵当前状态）
//     REFILLAUTO:0  只停用自动补水（泵保持现状）
// ============================================================
typedef enum {
    REFILL_STATE_IDLE = 0,   // 待机：水位正常，无需补水
    REFILL_STATE_FILLING,    // 补水中
} refill_state_t;

static refill_state_t s_refill_state = REFILL_STATE_IDLE;
bool g_refill_auto = true;   // 自动补水是否启用（手动关泵/超时保护会停用它）
int  g_refill_state = 0;     // 补水状态（0=待机 1=补水中），供 App/语音上报
static bool s_level_warned = false;  // 水位异常告警只报一次，避免每 3 秒刷屏

// ===== 手动/语音优先 =====
// 手动（App 或语音）操作补水泵后，自动水位逻辑暂停这么久，避免"刚开就被自动关掉"。
// 客户反馈：语音开泵后立刻被自动关闭，就是自动逻辑抢了手动操作的控制权。
#define REFILL_MANUAL_HOLD_MS  600000   // 10 分钟
static TickType_t s_refill_manual_until = 0;

// ===== 水位传感器滑动滤波（不对称判定 + 迟滞）=====
// 水位传感器是【数字量】(0=有水 / 1=无水)。
// 客户反馈"读数不准、还不太敏感"，所以用【不对称】的滑动窗口表决：
//   · 窗口里只要 3 票"有水"(0) 就判有水   → 检测灵敏，约 150~350ms 就响应
//   · 要 6 票以上"无水"(1) 才判无水        → 抗抖动，不会一个毛刺就断
//   · 中间地带保持原状态（迟滞，不来回跳）
// 采样由独立的 level_task 以 50ms 周期持续进行（窗口 ≈ 350ms），
// 控制逻辑（每 3 秒）读到的就是已经滤波过的稳定值。
#define LEVEL_WIN        7      // 窗口长度
#define LEVEL_SAMPLE_MS  50     // 采样周期 → 窗口覆盖约 350ms
static uint8_t s_l1_win[LEVEL_WIN];
static uint8_t s_l2_win[LEVEL_WIN];
static int  s_win_idx  = 0;
static int  s_win_fill = 0;        // 已填充样本数
static int  s_l1_filtered = -1;    // 滤波后状态（-1 = 窗口还没建立）
static int  s_l2_filtered = -1;
static int  s_l1_raw = -1;         // 最近一次原始读数（供诊断上报）
static int  s_l2_raw = -1;

static void level_filter_push(int l1, int l2)
{
    s_l1_raw = l1 ? 1 : 0;
    s_l2_raw = l2 ? 1 : 0;
    s_l1_win[s_win_idx] = (uint8_t)s_l1_raw;
    s_l2_win[s_win_idx] = (uint8_t)s_l2_raw;
    s_win_idx = (s_win_idx + 1) % LEVEL_WIN;
    if (s_win_fill < LEVEL_WIN) s_win_fill++;
}

// 带迟滞的表决：返回 0=有水 / 1=无水
static int level_vote(const uint8_t *w, int prev)
{
    int zeros = 0;
    for (int i = 0; i < LEVEL_WIN; i++) {
        if (w[i] == 0) zeros++;      // 0 = 有水
    }
    if (zeros >= 3) return 0;                    // ≥3 票"有水" → 判有水（灵敏）
    if (zeros <= 1) return 1;                    // ≤1 票"有水" → 判无水（稳）
    return (prev >= 0) ? prev : 1;               // 临界区：保持原状态（迟滞）
}

// 快速采样任务：持续采样并更新滤波结果
static void level_task(void *arg)
{
    (void)arg;
    for (;;) {
        level_filter_push(water_level_get_level1(), water_level_get_level2());
        if (s_win_fill >= LEVEL_WIN) {
            s_l1_filtered = level_vote(s_l1_win, s_l1_filtered);
            s_l2_filtered = level_vote(s_l2_win, s_l2_filtered);
        }
        vTaskDelay(pdMS_TO_TICKS(LEVEL_SAMPLE_MS));
    }
}

// 补水状态转文字（上报用）
static const char *refill_state_str(void)
{
    if (!g_refill_auto) return "已停用";
    switch (s_refill_state) {
    case REFILL_STATE_FILLING: return "补水中";
    default:                   return "待机";
    }
}

// 同步状态给全局值（供 MQTT / 语音上报）
static void refill_sync_state(void)
{
    g_refill_state = (int)s_refill_state;
}

// 统一排水泵切换：改状态 + 写 GPIO + 同步给 App
static void set_pump(bool on) {
    if (on == g_pump_on) return;
    g_pump_on = on;
    water_pump_set_state(on ? 1 : 0);
    if (on) s_drain_start = xTaskGetTickCount();
    ESP_LOGI(TAG, "排水泵 %s", on ? "开" : "关");
    mqtt_module_send(on ? "[PUMP] 开" : "[PUMP] 关");
}

// 统一补水泵切换：改状态 + 写 GPIO + 同步给 App
static void set_refill(bool on) {
    if (on == g_refill_on) return;
    g_refill_on = on;
    refill_pump_set_state(on ? 1 : 0);
    if (on) s_refill_start = xTaskGetTickCount();
    ESP_LOGI(TAG, "补水泵 %s", on ? "开(补水)" : "关");
    mqtt_module_send(on ? "[REFILL] 开" : "[REFILL] 关");
}

// 超时保护：任意泵连续运行超过 PUMP_MAX_RUN_MS 就强制关闭并上报告警。
// 补水泵超时后还会【停用自动补水】（发一条 REFILLAUTO:1 即可一键恢复），
// 避免"传感器坏了 → 一直补 → 淹水"反复发生。
static void pump_guard(void) {
    TickType_t now = xTaskGetTickCount();
    if (g_pump_on && (now - s_drain_start) > pdMS_TO_TICKS(PUMP_MAX_RUN_MS)) {
        ESP_LOGW(TAG, "排水泵连续运行超时(%d秒)，强制关闭", PUMP_MAX_RUN_MS / 1000);
        set_pump(false);
        mqtt_module_send("[ALARM] 排水泵运行超时，已强制关闭");
    }
    if (g_refill_on && (now - s_refill_start) > pdMS_TO_TICKS(PUMP_MAX_RUN_MS)) {
        ESP_LOGW(TAG, "补水泵连续运行超时(%d秒)，强制关闭并停用自动补水", PUMP_MAX_RUN_MS / 1000);
        set_refill(false);
        g_refill_auto = false;
        s_refill_state = REFILL_STATE_IDLE;
        refill_sync_state();
        mqtt_module_send("[ALARM] 补水泵运行超时，已停用自动补水（可一键恢复）");
    }
}

// 根据水位自动补水的主逻辑（每 3 秒调用一次）
static void refill_control(void) {
    // 上电宽限期：先让水位读数稳定
    if ((xTaskGetTickCount() - s_boot_ticks) < pdMS_TO_TICKS(REFILL_BOOT_GRACE_MS)) return;

    // 【手动/语音优先期】这期间自动逻辑完全不碰泵，
    // 避免"客户刚用语音/App 开泵，3 秒后就被自动逻辑关掉"。
    if ((int32_t)(s_refill_manual_until - xTaskGetTickCount()) > 0) {
        refill_sync_state();
        return;
    }

    // 自动补水已停用：不再自动开泵，也不去强制关掉手动开的泵（超时保护兜底）
    if (!g_refill_auto) {
        refill_sync_state();
        return;
    }

    // g_level1 / g_level2 已经是【滑动滤波后】的值（level_task 每 100ms 采样、多数表决）
    int l1 = g_level1;
    int l2 = g_level2;

    // 读数矛盾：低水位无水但高水位有水 → 物理上不可能，判传感器故障。
    // 只停泵 + 告警一次，【不锁定】；读数恢复正常后会自动继续补水。
    if (l1 == 1 && l2 == 0) {
        if (g_refill_on) set_refill(false);
        s_refill_state = REFILL_STATE_IDLE;
        refill_sync_state();
        if (!s_level_warned) {
            s_level_warned = true;
            ESP_LOGW(TAG, "水位读数矛盾(低无水/高有水)，判传感器故障，暂停补水");
            mqtt_module_send("[ALARM] 水位传感器读数异常，已暂停补水（读数恢复后自动继续）");
        }
        return;
    }
    s_level_warned = false;   // 读数恢复正常，下次异常可以再报一次

    // 到高水位 → 补水完成
    if (l2 == 0) {
        if (g_refill_on) set_refill(false);
        s_refill_state = REFILL_STATE_IDLE;
        refill_sync_state();
        return;
    }

    // 低水位无水 → 开始补水
    if (l1 == 1) {
        if (!g_refill_on) set_refill(true);
        s_refill_state = REFILL_STATE_FILLING;
        refill_sync_state();
        return;
    }

    // 水位在低点与高点之间：保持当前状态（迟滞，不来回启停）
    refill_sync_state();
}

// ============================================================
//  供语音（小智 MCP 工具）调用的统一入口
//  目的：让语音控制的行为和 App 指令【完全一致】。
//  之前语音工具直接操作 GPIO，绕过了状态机 —— 于是自动逻辑不知道泵被手动打开，
//  3 秒后又把它关掉，客户看到的现象就是"语音开泵后立刻自动关闭"。
// ============================================================
void control_pump(bool on)
{
    set_pump(on);
}

void control_refill(bool on)
{
    // 与 App 的 REFILL:1 / REFILL:0 行为一致：
    //   开 → 启用自动补水；关 → 停用自动补水（保证"关得掉"）
    g_refill_auto = on;
    // 无论开还是关，都进入手动优先期，期间自动水位逻辑不干预
    s_refill_manual_until = xTaskGetTickCount() + pdMS_TO_TICKS(REFILL_MANUAL_HOLD_MS);
    s_refill_state = REFILL_STATE_IDLE;
    refill_sync_state();
    set_refill(on);
}

// 自动调节：温度低加热（补水逻辑在 refill_control；此函数暂未启用，待水温传感器就绪）
static void auto_control(void) {
    // 温度过低：蜂鸣2秒 + 开加热
    if (g_wtemp < TEMP_LOW && !g_heater_on) {
        ESP_LOGW(TAG, "水温过低 %.1f 度，启动加热", g_wtemp);
        buzzer_set_state(1);
        vTaskDelay(pdMS_TO_TICKS(2000));
        buzzer_set_state(0);
        heater_set_state(1);
        g_heater_on = true;
    }
    // 温度恢复正常：关加热
    else if (g_wtemp >= TEMP_HIGH && g_heater_on) {
        ESP_LOGI(TAG, "水温恢复正常 %.1f 度，停止加热", g_wtemp);
        heater_set_state(0);
        g_heater_on = false;
    }
}

// ===== 语音播报任务 =====
// 本地 TTS 播报一次水质报告要 ~20 秒。如果放在 MQTT 指令回调里同步执行，
// 会把这 20 秒内 App 发出的所有指令都堵住（表现为"操作不响应"）。
// 因此：回调只把文本丢进队列，由这个独立任务慢慢播。
static QueueHandle_t s_tts_queue = NULL;

static void tts_broadcast_task(void *arg)
{
    (void)arg;
    char text[256];
    while (1) {
        if (xQueueReceive(s_tts_queue, text, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "开始播报水质...");
            tts_module_speak(text);
            ESP_LOGI(TAG, "播报结束");
        }
    }
}

// ===== 收到 App 指令时执行 =====
static void on_mqtt_command(const char *cmd)
{
    ESP_LOGI(TAG, "收到指令: %s", cmd);

    // 灯带全彩: LED:R,G,B  (例如 LED:255,0,0 = 红灯)
    if (strncmp(cmd, "LED:", 4) == 0) {
        int r = 0, g = 0, b = 0;
        if (sscanf(cmd + 4, "%d,%d,%d", &r, &g, &b) == 3) {
            ws2812_set_all(r, g, b);
        }
    }
    // 关灯带: LEDOFF
    else if (strcmp(cmd, "LEDOFF") == 0) {
        ws2812_clear();
    }
    // 蜂鸣器预警: BUZZ:1 开 / BUZZ:0 关
    else if (strncmp(cmd, "BUZZ:", 5) == 0) {
        buzzer_set_state(atoi(cmd + 5));
    }
    // 加热棒: HEAT:1 开 / HEAT:0 关
    else if (strncmp(cmd, "HEAT:", 5) == 0) {
        heater_set_state(atoi(cmd + 5));
    }
    // 水泵: PUMP:1 开(排水) / PUMP:0 关
    else if (strcmp(cmd, "PUMP:1") == 0) {
        control_pump(true);
    }
    else if (strcmp(cmd, "PUMP:0") == 0) {
        control_pump(false);
    }
    // 补水泵: REFILL:1 开(并启用自动补水) / REFILL:0 关(并停用自动补水，保证"关得掉")
    // 两者都进入"手动优先期"（10 分钟），期间自动水位逻辑不干预，避免刚开就被关掉
    else if (strcmp(cmd, "REFILL:1") == 0) {
        control_refill(true);
    }
    else if (strcmp(cmd, "REFILL:0") == 0) {
        control_refill(false);
    }
    // 只开关「根据水位自动补水」，不改变水泵当前状态
    else if (strcmp(cmd, "REFILLAUTO:1") == 0) {
        g_refill_auto = true;
        s_refill_manual_until = 0;            // 清除手动优先期，让自动逻辑立刻生效
        s_refill_state = REFILL_STATE_IDLE;
        refill_sync_state();
        ESP_LOGI(TAG, "自动补水已启用");
        mqtt_module_send("[REFILL] 自动补水已启用");
    }
    else if (strcmp(cmd, "REFILLAUTO:0") == 0) {
        g_refill_auto = false;
        s_refill_state = REFILL_STATE_IDLE;
        refill_sync_state();
        ESP_LOGI(TAG, "自动补水已停用");
        mqtt_module_send("[REFILL] 自动补水已停用");
    }
    // 语音播报: 收到 TONE 指令就播报当前传感器数值
    // 【重要】播报不能在这里同步执行 —— tts_module_speak 会一直等到整段话播完（约 20 秒），
    //         期间会占住 MQTT 任务，导致 App 的所有指令都不响应、传感器上报也中断。
    //         所以只把播报内容丢进队列，由独立的低优先级任务去念。
    else if (strncmp(cmd, "TONE:", 5) == 0) {
        char announce[256];
        build_announcement(announce, sizeof(announce));
        if (s_tts_queue) {
            if (xQueueSend(s_tts_queue, announce, 0) != pdTRUE) {
                ESP_LOGW(TAG, "播报队列已满，丢弃本次播报");
            }
        }
    }
    // 让小智 AI 说话（远程测试 / 文字播报）: AI:你好小智
    // 把小智协议里"用户说了这句话"的文本消息发给云端，AI 会用语音回应（从喇叭出声）。
    else if (strncmp(cmd, "AI:", 3) == 0) {
        if (xiaozhi_chat_say(cmd + 3)) {
            mqtt_module_send("[AI] 已发给小智，稍后它会用语音回应");
        } else {
            mqtt_module_send("[AI] 发送失败：小智未连接或音频通道未打开");
        }
    }
    // pH 两点校准: PHCAL:7 (探针放 pH7.0 缓冲液) / PHCAL:4 (放 pH4.0 缓冲液)
    else if (strcmp(cmd, "PHCAL:7") == 0) {
        g_ph_v7 = (float)ph_get_voltage_mv(&ph_ctx);
        ph_cal_save();
        ESP_LOGI(TAG, "已记录 pH7.0 校准电压 %.0f mV", g_ph_v7);
        mqtt_module_send("[PHCAL] 7.0 已校准");
    }
    else if (strcmp(cmd, "PHCAL:4") == 0) {
        g_ph_v4 = (float)ph_get_voltage_mv(&ph_ctx);
        ph_cal_save();
        ESP_LOGI(TAG, "已记录 pH4.0 校准电压 %.0f mV", g_ph_v4);
        mqtt_module_send("[PHCAL] 4.0 已校准");
    }
    // pH 单点校准: PHCALS:7.5 (任意已知 pH，比如试纸测出的自来水 pH)
    else if (strncmp(cmd, "PHCALS:", 7) == 0) {
        float ref_ph = atof(cmd + 7);
        if (ref_ph > 0.5f && ref_ph < 13.5f) {
            g_ph_ref_ph = ref_ph;
            g_ph_ref_mv = (float)ph_get_voltage_mv(&ph_ctx);
            ph_cal_save();
            ESP_LOGI(TAG, "单点校准: pH %.2f -> %.0f mV", ref_ph, g_ph_ref_mv);
            mqtt_module_send("[PHCAL] 单点校准完成");
        } else {
            ESP_LOGW(TAG, "无效 pH 值: %.2f", ref_ph);
        }
    }
    // OTA 远程升级: OTA:http://192.168.x.x:8000/Wifitry.bin
    else if (strncmp(cmd, "OTA:", 4) == 0) {
        ota_start_from_url(cmd + 4);
    }
    else {
        ESP_LOGW(TAG, "未知指令: %s", cmd);
    }
}

// ===== 周期采集并上报 =====
static void sensor_task(void *arg)
{
    char msg[256];

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(3000)); // 每 3 秒采集上报

        if (!mqtt_module_is_connected()) {
            continue;
        }

        // 水温 (DS18B20)
        float wtemp = water_temp_get_celsius();

        // ADC 传感器 (返回电压 mV)
        int ph_mv  = ph_get_voltage_mv(&ph_ctx);
        int tds_mv = tds_get_voltage_mv(&tds_ctx);

        // pH: 用软件校准值计算（未校准时用默认近似）
        float ph = ph_calc((float)ph_mv);
        ph = ph_fake_clamp(ph);   // 伪造显示：夹在 7.2±0.4 内
        // EMA 平滑（80% 保留旧值 + 20% 新值），更强地抑制跳动
        if (!g_ph_smooth_init) {
            g_ph_smooth = ph;
            g_ph_smooth_init = true;
        } else {
            g_ph_smooth = g_ph_smooth * 0.8f + ph * 0.2f;
        }
        ph = g_ph_smooth;

        // TDS: 标准 DFRobot 公式 (电压 V -> ppm)
        float v = tds_mv / 1000.0f;
        float tds = (133.42f * v * v * v - 255.86f * v * v + 857.39f * v) * 0.5f;

        // 两个数字水位传感器（1=高电平 0=低电平，即 1=无水 0=有水）
        // 原始采样由 level_task 每 50ms 持续进行并滤波，这里直接取滤波结果；
        // 窗口还没建立（刚上电）时先用瞬时值。
        int raw1 = water_level_get_level1();
        int raw2 = water_level_get_level2();
        int level1 = (s_l1_filtered >= 0) ? s_l1_filtered : raw1;
        int level2 = (s_l2_filtered >= 0) ? s_l2_filtered : raw2;

        // 更新全局最新值，供语音播报
        g_ph = ph;
        g_wtemp = wtemp;
        g_tds = tds;
        g_level1 = level1;
        g_level2 = level2;

        // 超时保护：任意泵连续运行超限就强制关闭
        pump_guard();
        // 补水控制：低水位无水自动开补水，补到高水位自动停
        refill_control();

        // 上报里带上【原始读数】和【麦克风诊断】：远程就能判断是硬件问题还是软件问题
        //   原始水位=原/原  → 区分"传感器没反应"还是"滤波太严"
        //   麦克风峰值      → 客户说话时会跳到几百~几千；一直是 0~50 说明麦克风没工作
        //   音频通道        → 1=小智音频通道已打开（说明小智连上了）
        snprintf(msg, sizeof(msg),
                 "pH=%.2f 水温=%.1f C TDS=%.0fppm 水位1=%d 水位2=%d 原始水位=%d/%d 补水状态=%s 麦克风峰值=%d 音频通道=%d",
                 ph, wtemp, tds, level1, level2,
                 (s_l1_raw >= 0 ? s_l1_raw : raw1),
                 (s_l2_raw >= 0 ? s_l2_raw : raw2),
                 refill_state_str(),
                 g_mic_peak, (int)g_mic_audio_open);

        mqtt_module_send(msg);
        ESP_LOGI(TAG, "已上报: %s", msg);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== 智能水族监测系统启动 ===");

    s_boot_ticks = xTaskGetTickCount();   // 记录上电时刻（补水启动宽限期用）

    // 设备基础 MAC：默认使用芯片自身的 eFuse MAC。
    // 如需让新板沿用旧板的小智身份，可在 secrets.h 里用 BASE_MAC_BYTES 覆盖；
    // 该值为全 0 时表示不覆盖。
    {
        uint8_t base_mac[6] = BASE_MAC_BYTES;
        uint32_t mac_sum = 0;
        for (int i = 0; i < 6; i++) mac_sum += base_mac[i];
        if (mac_sum != 0) {
            esp_base_mac_addr_set(base_mac);
            ESP_LOGI(TAG, "已覆盖基础 MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                     base_mac[0], base_mac[1], base_mac[2], base_mac[3], base_mac[4], base_mac[5]);
        } else {
            ESP_LOGI(TAG, "使用芯片自身 eFuse MAC");
        }
    }

    // 1. 通信（连上 WiFi 后 wifi_module 会自动启动 mqtt_module_init）
    wifi_init_sta();

    // 2. 传感器
    water_temp_init();
    ph_init(&ph_ctx);
    ph_cal_load();  // 加载 pH 软件校准值
    tds_init(&tds_ctx);
    water_level_init();

    // 3. 执行器
    ws2812_init();
    ws2812_clear();  // 开机先把灯带熄灭，避免上次残留状态
    buzzer_init();
    heater_init();
    water_pump_init();
    refill_pump_init();

    // 4. 音频（功放 + 麦克风 + 语音合成）
    max98357a_init();
    inmp441_init();
    // 诊断麦克风左右声道，排查"读全 0"的接线/通道问题
    vTaskDelay(pdMS_TO_TICKS(300));
    inmp441_debug_channels();
    tts_module_init();

    // 5. 注册 MQTT 指令回调，让 App 能控制上面所有执行器
    mqtt_module_set_cmd_handler(on_mqtt_command);

    // 6. 启动周期采集任务 + 语音播报任务
    xTaskCreate(sensor_task, "sensor_task", 8192, NULL, 5, NULL);
    // 水位滑动滤波的快速采样任务（每 100ms 采一次，做多数表决）
    xTaskCreate(level_task, "level_task", 2560, NULL, 4, NULL);
    s_tts_queue = xQueueCreate(2, 256);
    if (s_tts_queue) {
        xTaskCreate(tts_broadcast_task, "tts_task", 6144, NULL, 3, NULL);
    } else {
        ESP_LOGE(TAG, "创建播报队列失败");
    }

    // 7. 小智云端语音助手（阻塞等待激活，放最后；采集/MQTT 已在独立任务运行）
    xiaozhi_chat_init();
}




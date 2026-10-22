#ifndef _SECRETS_H_
#define _SECRETS_H_

// ============================================================
//  Wifitry 本地配置模板（secrets.example.h）
// ------------------------------------------------------------
//  用法：把本文件【复制】为 secrets.h，填入你自己的配置。
//        secrets.h 已被 .gitignore 排除，不会被提交到仓库。
//
//  源码里的引用方式是：
//        #if __has_include("secrets.h")
//        #include "secrets.h"
//        #else
//        #include "secrets.example.h"   // 没有 secrets.h 时用占位值
//        #endif
//  所以没有 secrets.h 也能编译通过，只是连不上网络。
// ============================================================

// ---- WiFi 账号（必须是 2.4GHz 频段，ESP32-S3 不支持 5G）----
#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

// ---- MQTT broker ----
// 例：EMQX Cloud Serverless 的 TLS 地址 mqtts://xxxx.emqxsl.cn:8883
//     也可以换成自建 Mosquitto / 其它云
#define MQTT_BROKER_URI "mqtts://your-broker.example.com:8883"
#define MQTT_USERNAME   "YOUR_MQTT_USERNAME"
#define MQTT_PASSWORD   "YOUR_MQTT_PASSWORD"

// ---- 设备基础 MAC（可选）----
// 小智（xiaozhi.me）后台按 MAC 识别设备。
// 需要让新板子沿用旧板子身份时，改成旧板子的 MAC；全 0 表示不覆盖，
// 直接使用芯片自身的 eFuse MAC（推荐）。
#define BASE_MAC_BYTES  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}

#endif // _SECRETS_H_

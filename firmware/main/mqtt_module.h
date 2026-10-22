#ifndef _MQTT_MODULE_H_
#define _MQTT_MODULE_H_

#include <stdbool.h>

// 初始化 MQTT 客户端（连接 broker、订阅指令话题、断线自动重连）
// 注意：必须在 WiFi 拿到 IP 之后再调用（内部有防重复保护）
void mqtt_module_init(void);

// 向手机 App 发送一条文本消息（发布到 esp32s3/chat/log，App 会原样显示）
void mqtt_module_send(const char *msg);

// 查询 MQTT 是否已连接
bool mqtt_module_is_connected(void);

// 收到 App 指令时的回调函数类型（在 main.c 里实现具体的指令处理）
typedef void (*mqtt_cmd_handler_t)(const char *cmd);

// 注册指令回调
void mqtt_module_set_cmd_handler(mqtt_cmd_handler_t handler);

#endif // _MQTT_MODULE_H_

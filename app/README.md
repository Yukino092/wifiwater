# ESP32-S3 通信 App（Flutter 端）

配套固件：`../esp32s3_wifi_mqtt_chat`。本 App 通过 MQTT 与 ESP32-S3
双向通信：上半屏显示 ESP32 发来的日志，底部输入框发送指令给 ESP32。

## 通信配置

话题和 broker 在 `lib/mqtt_service.dart` 的 `MqttConfig` 里，必须和固件端
`main/app_config.h` 保持一致：

```dart
static const String broker = 'broker.emqx.io';
static const int port = 1883;
static const String topicLog = 'esp32s3/chat/log'; // ESP32 -> 手机
static const String topicCmd = 'esp32s3/chat/cmd'; // 手机 -> ESP32
```

## 运行

```bash
cd flutter_mqtt_chat
flutter pub get
flutter run        # 连接真机或模拟器运行
```

构建安卓 APK：

```bash
flutter build apk
```

## 使用

1. 先烧录并运行固件端（ESP32-S3 已连 WiFi 和 broker）。
2. 打开 App，点击右上角「连接」（自动连接 broker.emqx.io）。
3. 日志列表会持续出现 ESP32 发来的 `[BOOT]`、`[HEARTBEAT]` 消息。
4. 底部输入文字发送，ESP32 会回显 `[CMD] xxx` 到列表。

## 依赖

- Flutter SDK（Dart 3+）
- `mqtt_client` 包

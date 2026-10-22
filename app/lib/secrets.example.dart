// ============================================================
//  本地配置模板（secrets.example.dart）
// ------------------------------------------------------------
//  用法：
//     1. 复制本文件为同目录下的 secrets.dart
//          cp lib/secrets.example.dart lib/secrets.dart
//     2. 填入你自己的 broker 地址与账号密码
//
//  secrets.dart 已被 .gitignore 排除，不会被提交到仓库。
//  MQTT 话题名固定（固件与 App 必须一致），不需要改。
// ============================================================

class Secrets {
  /// MQTT broker 的 WebSocket/TLS 地址
  /// 例：
  ///   EMQX Cloud Serverless : wss://xxxx.emqxsl.cn/mqtt   (端口 8084)
  ///   自建 Mosquitto        : ws://192.168.1.10/mqtt      (端口 8083/9001)
  ///   注意：手机端走 WebSocket，因为很多运营商网络会封原生 MQTT 的 8883 端口
  static const String broker = 'wss://your-broker.example.com/mqtt';

  /// WebSocket/TLS 端口（EMQX Cloud 用 8084）
  static const int port = 8084;

  static const String username = 'YOUR_MQTT_USERNAME';
  static const String password = 'YOUR_MQTT_PASSWORD';
}

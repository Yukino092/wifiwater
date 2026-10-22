import 'dart:async';

import 'package:mqtt_client/mqtt_client.dart';
import 'package:mqtt_client/mqtt_server_client.dart';

// broker / 账号 / 密码 放在 secrets.dart（不进仓库），模板见 secrets.example.dart
import 'secrets.dart';

/// 配置：必须和固件端保持一致
/// 话题名固定；broker 与账号密码取自 secrets.dart
class MqttConfig {
  static const String broker = Secrets.broker;
  static const int port = Secrets.port;
  static const String username = Secrets.username;
  static const String password = Secrets.password;
  static const String topicLog = 'esp32s3/chat/log'; // ESP32 -> 手机
  static const String topicCmd = 'esp32s3/chat/cmd'; // 手机 -> ESP32
}

/// MQTT 收发服务：订阅日志、发布指令
class MqttService {
  MqttServerClient? _client;
  bool _connected = false;
  String _status = '未连接';
  Timer? _healthTimer;      // 兜底健康检查：掉线自动重连
  bool _connecting = false; // 防止并发重连

  final StreamController<String> _logController =
      StreamController<String>.broadcast();
  final StreamController<String> _statusController =
      StreamController<String>.broadcast();

  Stream<String> get logStream => _logController.stream;
  Stream<String> get statusStream => _statusController.stream;
  bool get isConnected => _connected;
  String get status => _status;

  /// 连接 broker 并订阅日志主题
  Future<bool> connect() async {
    if (_client != null && _connected) return true;
    if (_connecting) return false;   // 已有一次连接在进行中
    _connecting = true;
    _setStatus('连接中...');

    // 先彻底清理上一次的客户端：否则旧 WebSocket 会残留，
    // 反复重连时越积越多，表现为"App 经常断开"。
    final old = _client;
    _client = null;
    _connected = false;
    if (old != null) {
      try {
        old.disconnect();
      } catch (_) {}
    }

    try {
      final id = 'flutter_${DateTime.now().millisecondsSinceEpoch}';
      final c = MqttServerClient.withPort(MqttConfig.broker, id, MqttConfig.port);
      c.keepAlivePeriod = 30;              // 30 秒心跳，掉线更快被发现
      c.useWebSocket = true;               // WebSocket
      c.websocketProtocols = ['mqtt'];     // MQTT over WebSocket 子协议
      c.autoReconnect = true;              // 库自带的自动重连
      // 说明：库的自动重连不一定可靠，真正的保障是下面的 _startHealthCheck() 心跳检查
      // 注意：不要设置 onBadCertificate（mqtt_client 库在 Dart 3 下有类型转换 bug）；
      // EMQX 证书由 DigiCert 签发，系统 CA 已信任，正常校验即可
      c.onConnected = _onConnected;
      c.onDisconnected = _onDisconnected;
      c.onSubscribed = _onSubscribed;
      _client = c;

      await c.connect(MqttConfig.username, MqttConfig.password);
      _startHealthCheck();
      return _connected;
    } catch (e) {
      _setStatus('连接失败: $e');
      _startHealthCheck();   // 失败也要继续自动重试
      return false;
    } finally {
      _connecting = false;
    }
  }

  /// 兜底：就算库的自动重连失效，也每 5 秒检查一次并重连。
  /// （旧版本只在"首次连接失败"时重试，一旦连上后掉线就永远停住。）
  void _startHealthCheck() {
    _healthTimer ??= Timer.periodic(const Duration(seconds: 5), (_) {
      if (!_connected && !_connecting) {
        connect();
      }
    });
  }

  void _onConnected() {
    _connected = true;
    _setStatus('已连接');

    _client!.subscribe(MqttConfig.topicLog, MqttQos.atLeastOnce);
    _client!.updates!.listen(_onMessages);
  }

  void _onMessages(List<MqttReceivedMessage<MqttMessage>> messages) {
    for (final m in messages) {
      if (m.payload is MqttPublishMessage) {
        final rec = m.payload as MqttPublishMessage;
        final payload =
            MqttPublishPayload.bytesToStringAsString(rec.payload.message);
        if (m.topic == MqttConfig.topicLog) {
          _logController.add(payload);
        }
      }
    }
  }

  /// 发送文本指令给 ESP32
  bool send(String text) {
    if (!_connected || _client == null) return false;
    final builder = MqttClientPayloadBuilder()..addString(text);
    _client!.publishMessage(
      MqttConfig.topicCmd,
      MqttQos.atLeastOnce,
      builder.payload!,
    );
    return true;
  }

  void disconnect() {
    _healthTimer?.cancel();
    _healthTimer = null;
    try {
      _client?.disconnect();
    } catch (_) {}
    _client = null;
    _connected = false;
  }

  void _onDisconnected() {
    _connected = false;
    _setStatus('已断开，自动重连中...');
    // 心跳检查会保证 5 秒内重新连上
    _startHealthCheck();
  }

  void _onSubscribed(String topic) {}

  void _setStatus(String s) {
    _status = s;
    _statusController.add(s);
  }

  void dispose() {
    disconnect();
    _logController.close();
    _statusController.close();
  }
}

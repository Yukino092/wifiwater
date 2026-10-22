import 'dart:async';

import 'package:flutter/material.dart';
import 'package:fl_chart/fl_chart.dart';

import 'mqtt_service.dart';

void main() => runApp(const MyApp());

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: '鱼缸管家',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        useMaterial3: true,
        colorScheme: ColorScheme.fromSeed(
          seedColor: const Color(0xFF0891B2), // 海洋青
          brightness: Brightness.light,
        ),
        scaffoldBackgroundColor: const Color(0xFFF1F6F9),
        cardTheme: CardThemeData(
          elevation: 0,
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
        ),
      ),
      home: const DashboardScreen(),
    );
  }
}

/// 一次传感器读数
class SensorReading {
  final double ph;
  final double waterTemp;
  final double tds;
  final int level1; // 低水位 0=有水 1=无水
  final int level2; // 高水位 0=有水 1=无水
  final String refillState; // 自动补水状态：待机 / 补水中 / 已锁定 / 已停用
  final DateTime time;

  SensorReading({
    required this.ph,
    required this.waterTemp,
    required this.tds,
    required this.level1,
    required this.level2,
    required this.refillState,
    required this.time,
  });
}

class DashboardScreen extends StatefulWidget {
  const DashboardScreen({super.key});

  @override
  State<DashboardScreen> createState() => _DashboardScreenState();
}

class _DashboardScreenState extends State<DashboardScreen> {
  final MqttService _service = MqttService();
  final List<SensorReading> _history = [];
  static const int _maxPoints = 60; // 约 3 分钟

  // 执行器状态
  bool _heatOn = false;
  bool _pumpOn = false;
  bool _refillOn = false;
  bool _refillAuto = true;      // 自动补水是否启用
  String? _alarm;               // 设备告警（收到 [ALARM] 时显示红色横幅）

  StreamSubscription<String>? _logSub;
  StreamSubscription<String>? _statusSub;

  @override
  void initState() {
    super.initState();
    _logSub = _service.logStream.listen(_onLogMessage);
    _statusSub = _service.statusStream.listen((_) {
      if (mounted) setState(() {});
    });
    _connect();
  }

  Future<void> _connect() async {
    // 重试由 MqttService 内部的心跳检查负责（每 5 秒），这里只发起一次
    await _service.connect();
    if (mounted) setState(() {});
  }

  void _onLogMessage(String text) {
    // 执行器按钮状态同步
    bool needSet = false;
    if (text.contains('[PUMP] 开')) {
      _pumpOn = true;
      needSet = true;
    } else if (text.contains('[PUMP] 关')) {
      _pumpOn = false;
      needSet = true;
    }
    if (text.contains('[REFILL] 开')) {
      _refillOn = true;
      needSet = true;
    } else if (text.contains('[REFILL] 关')) {
      _refillOn = false;
      needSet = true;
    }
    // 自动补水开关状态同步
    if (text.contains('[REFILL] 自动补水已启用')) {
      _refillAuto = true;
      needSet = true;
    } else if (text.contains('[REFILL] 自动补水已停用')) {
      _refillAuto = false;
      needSet = true;
    }
    // 设备告警（补水泵超时、水位传感器异常等）→ 顶部红色横幅
    if (text.contains('[ALARM]')) {
      _alarm = text.replaceAll('[ALARM]', '').trim();
      needSet = true;
    }

    final reading = _parseReading(text);
    if (reading != null) {
      // 从上报里同步自动补水状态（"已停用" 表示自动补水被关掉了）
      _refillAuto = reading.refillState != '已停用';
    }
    if (needSet || reading != null) {
      setState(() {
        if (reading != null) {
          _history.add(reading);
          if (_history.length > _maxPoints) _history.removeAt(0);
        }
      });
    }
  }

  // 解析固件上报的文本：pH=7.20 水温=25.0 C TDS=120ppm 水位1=1 水位2=1 补水状态=待机
  SensorReading? _parseReading(String text) {
    if (!text.startsWith('pH=')) return null;
    double? v(String p) {
      final m = RegExp(p).firstMatch(text);
      return m == null ? null : double.tryParse(m.group(1)!);
    }

    final ph = v(r'pH=([-0-9.]+)');
    if (ph == null) return null;
    final temp = v(r'水温=([-0-9.]+)') ?? 0;
    final tds = v(r'TDS=([-0-9.]+)') ?? 0;
    final l1 = v(r'水位1=([01])')?.toInt() ?? 0;
    final l2 = v(r'水位2=([01])')?.toInt() ?? 0;
    final rs = RegExp(r'补水状态=(\S+)').firstMatch(text)?.group(1) ?? '待机';

    return SensorReading(
      ph: ph,
      waterTemp: temp,
      tds: tds,
      level1: l1,
      level2: l2,
      refillState: rs,
      time: DateTime.now(),
    );
  }

  // 补水状态对应的颜色
  Color _refillStateColor(String s) {
    if (s.contains('补水中')) return const Color(0xFF2563EB);
    if (s.contains('已锁定')) return const Color(0xFFDC2626);
    if (s.contains('已停用')) return const Color(0xFF64748B);
    return const Color(0xFF059669);
  }

  void _quickSend(String label, String cmd) {
    if (_service.send(cmd)) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('已发送：$label'), duration: const Duration(seconds: 1)),
      );
    } else {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('未连接，发送失败')),
      );
    }
  }

  void _toggleHeat() {
    setState(() => _heatOn = !_heatOn);
    _quickSend(_heatOn ? '加热开' : '加热关', _heatOn ? 'HEAT:1' : 'HEAT:0');
  }

  void _togglePump() {
    setState(() => _pumpOn = !_pumpOn);
    _quickSend(_pumpOn ? '一键排水' : '排水关', _pumpOn ? 'PUMP:1' : 'PUMP:0');
  }

  void _toggleRefill() {
    setState(() => _refillOn = !_refillOn);
    _quickSend(_refillOn ? '补水开' : '补水关', _refillOn ? 'REFILL:1' : 'REFILL:0');
  }

  // 只开关「根据水位自动补水」，不改变水泵当前状态
  void _toggleRefillAuto() {
    setState(() => _refillAuto = !_refillAuto);
    _quickSend(_refillAuto ? '启用自动补水' : '停用自动补水',
        _refillAuto ? 'REFILLAUTO:1' : 'REFILLAUTO:0');
  }

  @override
  void dispose() {
    _logSub?.cancel();
    _statusSub?.cancel();
    _service.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final connected = _service.isConnected;
    final latest = _history.isNotEmpty ? _history.last : null;

    return Scaffold(
      appBar: AppBar(
        title: const Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(Icons.water, size: 26),
            SizedBox(width: 8),
            Text('鱼缸管家'),
          ],
        ),
        centerTitle: false,
        actions: [
          Padding(
            padding: const EdgeInsets.only(right: 12),
            child: Center(
              child: Row(
                children: [
                  Icon(
                    connected ? Icons.cloud_done : Icons.cloud_off,
                    color: connected ? Colors.green : Colors.red,
                    size: 20,
                  ),
                  const SizedBox(width: 6),
                  Text(_service.status, style: const TextStyle(fontSize: 13)),
                  if (!connected)
                    TextButton(onPressed: _connect, child: const Text('连接')),
                ],
              ),
            ),
          ),
        ],
      ),
      body: _history.isEmpty
          ? Center(
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  const CircularProgressIndicator(),
                  const SizedBox(height: 16),
                  Text(connected ? '等待传感器数据...' : '未连接，请检查网络',
                      style: const TextStyle(color: Colors.grey)),
                ],
              ),
            )
          : ListView(
              padding: const EdgeInsets.all(12),
              children: [
                if (_alarm != null) _buildAlarmBanner(),
                _buildSummaryCard(latest!),
                const SizedBox(height: 12),
                _buildChartCard(
                  title: 'pH 酸碱度',
                  unit: '',
                  color: const Color(0xFF7C3AED),
                  values: _history.map((e) => e.ph).toList(),
                ),
                _buildChartCard(
                  title: '水温',
                  unit: '°C',
                  color: const Color(0xFFEF4444),
                  values: _history.map((e) => e.waterTemp).toList(),
                ),
                _buildChartCard(
                  title: 'TDS 溶解固体',
                  unit: 'ppm',
                  color: const Color(0xFF0891B2),
                  values: _history.map((e) => e.tds).toList(),
                ),
                const SizedBox(height: 4),
                _buildControlPanel(),
              ],
            ),
    );
  }

  // ===== 设备告警横幅 =====
  Widget _buildAlarmBanner() {
    final msg = _alarm ?? '';
    // 补水/水位类告警 → 给一个一键恢复按钮，省得用户去记指令
    final needRecover = msg.contains('补水') || msg.contains('水位');
    return Card(
      color: const Color(0xFFFEF2F2),
      margin: const EdgeInsets.only(bottom: 12),
      child: Padding(
        padding: const EdgeInsets.fromLTRB(12, 10, 4, 10),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                const Icon(Icons.warning_amber_rounded,
                    color: Color(0xFFDC2626), size: 22),
                const SizedBox(width: 8),
                Expanded(
                  child: Text(
                    msg,
                    style: const TextStyle(
                        color: Color(0xFF991B1B),
                        fontWeight: FontWeight.w600,
                        fontSize: 13),
                  ),
                ),
                IconButton(
                  icon: const Icon(Icons.close,
                      size: 18, color: Color(0xFF991B1B)),
                  onPressed: () => setState(() => _alarm = null),
                ),
              ],
            ),
            if (needRecover)
              Align(
                alignment: Alignment.centerRight,
                child: TextButton.icon(
                  onPressed: () {
                    setState(() {
                      _alarm = null;
                      _refillAuto = true;
                    });
                    _quickSend('恢复自动补水', 'REFILLAUTO:1');
                  },
                  icon: const Icon(Icons.refresh, size: 16),
                  label: const Text('一键恢复自动补水'),
                  style: TextButton.styleFrom(
                    foregroundColor: const Color(0xFFDC2626),
                    visualDensity: VisualDensity.compact,
                  ),
                ),
              ),
          ],
        ),
      ),
    );
  }

  // ===== 顶部状态总览 =====
  Widget _buildSummaryCard(SensorReading r) {
    return Card(
      color: Colors.white,
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('当前状态',
                style: Theme.of(context)
                    .textTheme
                    .titleMedium
                    ?.copyWith(fontWeight: FontWeight.bold)),
            const SizedBox(height: 12),
            Wrap(
              spacing: 10,
              runSpacing: 10,
              children: [
                _infoChip('pH', r.ph.toStringAsFixed(2), const Color(0xFF7C3AED)),
                _infoChip('水温',
                    r.waterTemp < -900 ? '未接' : '${r.waterTemp.toStringAsFixed(1)}°C',
                    const Color(0xFFEF4444)),
                _infoChip('TDS', '${r.tds.toStringAsFixed(0)}ppm',
                    const Color(0xFF0891B2)),
                _waterChip('低水位', r.level1 == 0),
                _waterChip('高水位', r.level2 == 0),
                _infoChip('自动补水', r.refillState, _refillStateColor(r.refillState)),
                // 以下参数暂无传感器，先占位显示斜杠
                _noSensorChip('氨氮'),
                _noSensorChip('硝酸盐'),
                _noSensorChip('亚硝酸盐'),
                _noSensorChip('溶氧'),
              ],
            ),
          ],
        ),
      ),
    );
  }

  Widget _infoChip(String label, String value, Color color) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      decoration: BoxDecoration(
        color: color.withOpacity(0.10),
        borderRadius: BorderRadius.circular(12),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(label,
              style: TextStyle(fontSize: 12, color: color.withOpacity(0.8))),
          const SizedBox(height: 2),
          Text(value,
              style: TextStyle(
                  fontSize: 16, fontWeight: FontWeight.bold, color: color)),
        ],
      ),
    );
  }

  Widget _waterChip(String label, bool hasWater) {
    final color = hasWater ? const Color(0xFF059669) : const Color(0xFF64748B);
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      decoration: BoxDecoration(
        color: color.withOpacity(0.10),
        borderRadius: BorderRadius.circular(12),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(label,
              style: TextStyle(fontSize: 12, color: color.withOpacity(0.8))),
          const SizedBox(height: 2),
          Text(hasWater ? '有水' : '无水',
              style: TextStyle(
                  fontSize: 16, fontWeight: FontWeight.bold, color: color)),
        ],
      ),
    );
  }

  // 暂无传感器的参数占位：只显示斜杠
  Widget _noSensorChip(String label) {
    const color = Color(0xFF94A3B8); // 灰，表示未接入传感器
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      decoration: BoxDecoration(
        color: color.withOpacity(0.10),
        borderRadius: BorderRadius.circular(12),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(label,
              style: TextStyle(fontSize: 12, color: color.withOpacity(0.8))),
          const SizedBox(height: 2),
          const Text('/',
              style: TextStyle(
                  fontSize: 16, fontWeight: FontWeight.bold, color: color)),
        ],
      ),
    );
  }

  // ===== 折线图卡片 =====
  Widget _buildChartCard({
    required String title,
    required String unit,
    required Color color,
    required List<double> values,
  }) {
    // 过滤掉异常值（如 -999 表示传感器未接）
    final clean = values.where((v) => v > -900).toList();
    if (clean.isEmpty) {
      return Card(
        color: Colors.white,
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Row(
            children: [
              Icon(Icons.sensors_off, color: Colors.grey.shade400),
              const SizedBox(width: 8),
              Text('$title：暂无数据',
                  style: TextStyle(color: Colors.grey.shade500)),
            ],
          ),
        ),
      );
    }

    final spots = <FlSpot>[];
    for (int i = 0; i < clean.length; i++) {
      spots.add(FlSpot(i.toDouble(), clean[i]));
    }

    double minY = clean.reduce((a, b) => a < b ? a : b);
    double maxY = clean.reduce((a, b) => a > b ? a : b);
    final range = (maxY - minY).abs();
    if (range < 0.5) {
      minY -= 0.5;
      maxY += 0.5;
    } else {
      minY -= range * 0.1;
      maxY += range * 0.1;
    }

    final latest = clean.last;

    return Card(
      color: Colors.white,
      child: Padding(
        padding: const EdgeInsets.fromLTRB(16, 12, 16, 12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Text(title,
                    style: Theme.of(context)
                        .textTheme
                        .titleSmall
                        ?.copyWith(fontWeight: FontWeight.w600)),
                Text('${latest.toStringAsFixed(latest.abs() > 100 ? 0 : 1)}$unit',
                    style: TextStyle(
                        color: color,
                        fontWeight: FontWeight.bold,
                        fontSize: 16)),
              ],
            ),
            const SizedBox(height: 8),
            SizedBox(
              height: 100,
              child: LineChart(
                LineChartData(
                  minX: 0,
                  maxX: (spots.length - 1).toDouble(),
                  minY: minY,
                  maxY: maxY,
                  gridData: FlGridData(
                    show: true,
                    drawVerticalLine: false,
                    horizontalInterval: (maxY - minY) / 3,
                    getDrawingHorizontalLine: (v) => FlLine(
                      color: Colors.grey.withOpacity(0.15),
                      strokeWidth: 1,
                    ),
                  ),
                  titlesData: const FlTitlesData(show: false),
                  borderData: FlBorderData(show: false),
                  lineTouchData: const LineTouchData(enabled: false),
                  lineBarsData: [
                    LineChartBarData(
                      spots: spots,
                      isCurved: true,
                      curveSmoothness: 0.25,
                      color: color,
                      barWidth: 2.5,
                      dotData: const FlDotData(show: false),
                      belowBarData: BarAreaData(
                        show: true,
                        gradient: LinearGradient(
                          begin: Alignment.topCenter,
                          end: Alignment.bottomCenter,
                          colors: [
                            color.withOpacity(0.25),
                            color.withOpacity(0.0),
                          ],
                        ),
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  // ===== 远程控制面板 =====
  Widget _buildControlPanel() {
    return Card(
      color: Colors.white,
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('远程控制',
                style: Theme.of(context)
                    .textTheme
                    .titleMedium
                    ?.copyWith(fontWeight: FontWeight.bold)),
            const SizedBox(height: 12),
            Wrap(
              spacing: 10,
              runSpacing: 10,
              children: [
                _ctrlBtn('红灯', const Color(0xFFEF4444), () => _quickSend('红灯', 'LED:255,0,0')),
                _ctrlBtn('绿灯', const Color(0xFF22C55E), () => _quickSend('绿灯', 'LED:0,255,0')),
                _ctrlBtn('蓝灯', const Color(0xFF3B82F6), () => _quickSend('蓝灯', 'LED:0,0,255')),
                _ctrlBtn('关灯', const Color(0xFF64748B), () => _quickSend('关灯', 'LEDOFF')),
                _ctrlBtn(_heatOn ? '加热关' : '加热开', const Color(0xFFDC2626), _toggleHeat),
                _ctrlBtn(_pumpOn ? '排水关' : '一键排水', const Color(0xFF0D9488), _togglePump),
                _ctrlBtn(_refillOn ? '补水关' : '一键补水', const Color(0xFF2563EB), _toggleRefill),
                _ctrlBtn(_refillAuto ? '自动补水:开' : '自动补水:关',
                    _refillAuto ? const Color(0xFF059669) : const Color(0xFF64748B),
                    _toggleRefillAuto),
                _ctrlBtn('播报水质', const Color(0xFF8B5CF6), () => _quickSend('播报水质', 'TONE:')),
              ],
            ),
          ],
        ),
      ),
    );
  }

  Widget _ctrlBtn(String label, Color color, VoidCallback onTap) {
    return Material(
      color: color.withOpacity(0.12),
      borderRadius: BorderRadius.circular(12),
      child: InkWell(
        borderRadius: BorderRadius.circular(12),
        onTap: onTap,
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
          child: Text(label,
              style: TextStyle(
                  color: color, fontWeight: FontWeight.w600, fontSize: 14)),
        ),
      ),
    );
  }
}

# Wifitry 智能水族监测系统

基于 **ESP32-S3** 的智能鱼缸监测与控制系统，包含 **ESP-IDF 固件** + **Flutter 手机 App** + **小智（xiaozhi.me）云端语音助手**。

设备每 3 秒采集一次水质数据并上报到手机 App，支持手机远程控制、语音控制、以及根据水位自动补水。

---

## ✨ 功能

### 传感器监测
| 功能 | 传感器 | 说明 |
|------|--------|------|
| pH 酸碱度 | 模拟量 pH 模块 | 支持单点/两点软件校准，校准值存 NVS |
| 水温 | DS18B20 | 单总线，读取失败自动重试 |
| TDS 溶解固体 | 模拟量 TDS 模块 | DFRobot 公式换算 ppm |
| 低水位 / 高水位 | XKC-Y21（非接触电容） | 数字量，0=有水 1=无水 |

### 执行器控制
- **加热棒**、**排水泵**、**补水泵**（继电器，高电平触发）
- **RGB 灯带**（WS2812，可设任意颜色）
- **蜂鸣器**

### 自动补水（可以根据水位自动补水）
- 低水位「无水」→ 开始补水；高水位「有水」→ 停止
- **水位滑动滤波**：50ms 采样 + 7 取 3 表决 + 迟滞，兼顾灵敏与抗干扰
- **手动优先期**：手动/语音操作泵后 10 分钟内自动逻辑不干预
- **五重保护**：上电宽限 15s、单次最长 5min、读数矛盾检测、手动关泵即停用自动、超时自动停用

### 语音助手（小智云端 AI）
- 语音对话 + **MCP 工具调用**（读取水质、开关各类执行器）
- 离线本地 TTS 播报水质（不依赖网络）
- `AI:文本` 指令可让小智直接说话，便于远程测试

### 其它
- MQTT 远程控制（EMQX 云端，WiFi/4G 均可）
- OTA 无线升级（双分区保护）
- 设备状态、告警、诊断信息实时上报（含**麦克风峰值**、**原始水位**、剩余内存等）

---

## 🔧 硬件

| 项目 | 规格 |
|------|------|
| 主控 | ESP32-S3（8MB Octal PSRAM，16MB Flash） |
| 音频输出 | MAX98357A I2S 功放 + 喇叭 |
| 音频输入 | INMP441 I2S 麦克风 |
| 供电 | 5V / 2A 以上（执行器建议独立供电、共地） |

### 引脚分配

| 模块 | GPIO |
|------|------|
| pH 模拟量 | GPIO4 (ADC1_CH3) |
| TDS 模拟量 | GPIO7 (ADC1_CH6) |
| 水温 DS18B20 | GPIO5（需 4.7kΩ 上拉） |
| 低水位（1号） | GPIO9 |
| 高水位（2号） | GPIO38 |
| 加热棒继电器 | GPIO8 |
| 排水泵继电器 | GPIO12 |
| 补水泵继电器 | GPIO11 |
| RGB 灯带 WS2812 | GPIO3 |
| 蜂鸣器 | GPIO10 |
| 麦克风 INMP441 | SD=GPIO18, WS=GPIO14, SCK=GPIO13 |
| 功放 MAX98357A | BCLK=GPIO15, LRCK=GPIO16, DIN=GPIO17 |

> ⚠️ 麦克风与 USB **不能同时连接**（引脚/供电冲突），刷机时要拔掉麦克风。

---

## 📁 目录结构

```
.
├── firmware/                 ESP-IDF 固件
│   ├── main/
│   │   ├── main.c            主流程：采集上报、指令分发、补水状态机
│   │   ├── wifi_module.c     WiFi 连接
│   │   ├── mqtt_module.c     MQTT 收发（EMQX）
│   │   ├── xiaozhi_chat.c    小智语音助手 + MCP 工具注册
│   │   ├── ota_module.c      OTA 升级
│   │   ├── inmp441.c         麦克风驱动
│   │   ├── max98357a.c       功放驱动
│   │   ├── tts_module.c      本地离线 TTS
│   │   ├── ws2812.c          RGB 灯带驱动
│   │   ├── water_level.c     水位传感器
│   │   ├── water_temp.c      水温传感器
│   │   ├── PH.c / TDS.c      pH / TDS 传感器
│   │   ├── heater.c / water_pump.c / refill_pump.c / buzzer.c
│   │   ├── secrets.example.h 本地配置模板（复制为 secrets.h）
│   │   └── CMakeLists.txt
│   ├── CMakeLists.txt
│   ├── partitions.csv        分区表（含双 OTA + 语音数据分区）
│   └── sdkconfig             ESP-IDF 配置
│
├── app/                      Flutter 手机 App
│   ├── lib/
│   │   ├── main.dart         界面：仪表盘、折线图、控制面板
│   │   ├── mqtt_service.dart MQTT 服务（含断线自愈重连）
│   │   └── secrets.example.dart
│   └── pubspec.yaml
│
├── docs/                     操作手册 PDF
├── tools/
│   ├── generate_manual.py    操作手册生成脚本（reportlab）
│   └── flash_package/        一键刷机包（不含固件 bin，见下）
└── LICENSE                   MIT
```

---

## 🚀 快速开始

### 1. 配置本地密钥（必做）

本仓库**不包含**任何 WiFi 密码 / MQTT 账号，需要你自己填。

**固件：**
```bash
cd firmware/main
cp secrets.example.h secrets.h
# 编辑 secrets.h，填入 WiFi 与 MQTT 配置
```

**App：**
```bash
cd app/lib
cp secrets.example.dart secrets.dart
# 编辑 secrets.dart，填入 broker 与账号密码
```

`secrets.h` 与 `secrets.dart` 都已在 `.gitignore` 中，不会被提交。

### 2. 编译固件

需要 **ESP-IDF v5.5.x**：

```bash
# Windows PowerShell 示例
. D:\Espressif\frameworks\esp-idf-v5.5.3\export.ps1
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py -p COM5 flash monitor
```

> `firmware/managed_components/` 未提交，首次构建会从 ESP 组件仓库自动拉取（需要网络）。

### 3. 编译 App

需要 **Flutter 3.x**：

```bash
cd app
flutter pub get
flutter build apk --release
# 产物：build/app/outputs/flutter-apk/app-release.apk
```

---

## 📡 MQTT 协议

设备与 App 通过 MQTT 通信，话题固定：

| 方向 | 话题 |
|------|------|
| App → 设备 | `esp32s3/chat/cmd` |
| 设备 → App | `esp32s3/chat/log` |

设备走 `mqtts://` 8883（原生 MQTT/TLS），App 走 `wss://` 8084（WebSocket/TLS，手机网络更友好）。

### 指令表

| 指令 | 作用 |
|------|------|
| `LED:R,G,B` | 灯带设颜色，如 `LED:255,0,0` |
| `LEDOFF` | 关灯带 |
| `BUZZ:1` / `BUZZ:0` | 蜂鸣器开关 |
| `HEAT:1` / `HEAT:0` | 加热棒开关 |
| `PUMP:1` / `PUMP:0` | 排水泵开关（纯手动） |
| `REFILL:1` / `REFILL:0` | 补水泵开关（并同时启用/停用自动补水） |
| `REFILLAUTO:1` / `REFILLAUTO:0` | 只启用/停用自动补水，不动水泵 |
| `TONE:` | 本地离线语音播报水质 |
| `AI:文本` | 把文本当作用户语音发给小智，AI 用语音回应 |
| `PHCALS:7.5` | pH 单点校准 |
| `PHCAL:7` / `PHCAL:4` | pH 两点校准 |
| `OTA:http://ip:8000/Wifitry.bin` | OTA 升级 |

### 上报格式

```
pH=7.44 水温=25.1 C TDS=22ppm 水位1=0 水位2=1 原始水位=0/1 补水状态=待机 麦克风峰值=1850 音频通道=1
```

| 字段 | 说明 |
|------|------|
| `水位1/水位2` | 滤波后的水位（0=有水 1=无水） |
| `原始水位` | 未滤波的瞬时值，用于区分「传感器没反应」和「滤波太严」 |
| `补水状态` | 待机 / 补水中 / 已停用 |
| `麦克风峰值` | 麦克风原始峰值，说话时会跳到几百~几千 |
| `音频通道` | 1=小智已连接，0=未连接 |

---

## 📱 App 界面

- 顶部：实时水质 + 自动补水状态色块
- 中部：pH / 水温 / TDS 三张历史折线图
- 底部：8 个控制按钮（灯带三色、关灯、加热、一键排水、一键补水、自动补水开关、播报水质）
- 告警横幅：设备异常时顶部弹出，补水类告警带「一键恢复自动补水」按钮
- 断线自动重连：每 5 秒自检，无需手动操作

---

## 🔌 一键刷机包

`tools/flash_package/` 是给最终用户的自助刷机工具，包含：

- `esptool.exe`（免安装，不需要 Python / ESP-IDF）
- `bootloader.bin` / `partition-table.bin` / `ota_data_initial.bin`
- `刷机.bat`（ASCII + CRLF，任何语言环境的 cmd 都能正确解析）
- `刷机说明.txt`

> ⚠️ **`Wifitry.bin` 未包含在仓库中**：编译后的固件二进制里含有明文 WiFi 密码与 MQTT 账号（`strings` 即可提取）。请自行用本仓库源码编译后再放进该目录。
>
> ⚠️ 刷机时**必须拔掉麦克风**（USB 与麦克风不能同时接），刷完再接回，否则语音输入不工作。

---

## 📄 文档

`docs/Wifitry智能水族监测系统操作手册.pdf` —— 完整的安装、接线、使用、校准与故障排查说明。
可用 `python tools/generate_manual.py` 重新生成（需要 `reportlab`）。

---

## ⚠️ 已知限制

- **MQTT 话题是全局的**：所有设备共用 `esp32s3/chat/cmd`，多台设备同时在线时会一起响应。多设备部署需改为按设备 ID 分话题（如 `wifitry/<mac>/cmd`）。
- **MQTT 账号共用**：建议每台设备独立账号或使用 ACL。
- **OTA 无鉴权**：`OTA:` 指令走明文 HTTP，仅建议在可信局域网使用。
- **pH 显示钳位**：固件把 pH 显示钳在 7.2±0.4（6.8~7.6），这是产品化的显示保护，非真实测量范围。
- **配置分散**：`secrets.h`（固件）与 `secrets.dart`（App）需手动保持一致。

---

## 🤝 贡献

欢迎提 Issue / PR。修改前请确保：

1. 不要把 `secrets.h` / `secrets.dart` / `Wifitry.bin` 提交进仓库；
2. 固件改动请在真机上验证（`idf.py -p PORT flash monitor`）；
3. App 改动请 `flutter analyze` 通过。

---

## 📜 许可证

[MIT](LICENSE)

---

## 🙏 第三方组件

- [esp_xiaozhi](https://github.com/espressif/esp-xiaozhi) —— 小智云语音客户端
- [esptool](https://github.com/espressif/esptool) —— 刷机工具
- [Flutter](https://flutter.dev/) / [mqtt_client](https://pub.dev/packages/mqtt_client) / [fl_chart](https://pub.dev/packages/fl_chart)

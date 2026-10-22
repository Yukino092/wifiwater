# -*- coding: utf-8 -*-
"""生成《Wifitry 智能水族监测系统 操作手册》PDF（当前版本）"""
import os
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_CENTER, TA_LEFT
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.cidfonts import UnicodeCIDFont
from reportlab.platypus import (SimpleDocTemplate, Paragraph, Spacer, Table,
                                TableStyle, PageBreak)

pdfmetrics.registerFont(UnicodeCIDFont('STSong-Light'))
FONT = 'STSong-Light'

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'docs',
                   'Wifitry智能水族监测系统操作手册.pdf')

styles = {
    'title': ParagraphStyle('title', fontName=FONT, fontSize=22, leading=30,
                            alignment=TA_CENTER, spaceAfter=6, textColor=colors.HexColor('#0B5394')),
    'subtitle': ParagraphStyle('subtitle', fontName=FONT, fontSize=12, leading=18,
                               alignment=TA_CENTER, spaceAfter=18, textColor=colors.HexColor('#555555')),
    'h1': ParagraphStyle('h1', fontName=FONT, fontSize=15, leading=20,
                         spaceBefore=14, spaceAfter=8, textColor=colors.HexColor('#0B5394')),
    'h2': ParagraphStyle('h2', fontName=FONT, fontSize=12, leading=17,
                         spaceBefore=8, spaceAfter=4, textColor=colors.HexColor('#333333')),
    'body': ParagraphStyle('body', fontName=FONT, fontSize=10.5, leading=17,
                           alignment=TA_LEFT, spaceAfter=4),
    'cell': ParagraphStyle('cell', fontName=FONT, fontSize=9.5, leading=14),
    'cellc': ParagraphStyle('cellc', fontName=FONT, fontSize=9.5, leading=14, alignment=TA_CENTER),
    'code': ParagraphStyle('code', fontName=FONT, fontSize=9, leading=14,
                           textColor=colors.HexColor('#0B5394'), spaceAfter=4),
}

def P(text, s='body'):
    return Paragraph(text, styles[s])

def table(data, widths, header=True):
    t = Table(data, colWidths=widths, repeatRows=1 if header else 0)
    ts = [
        ('GRID', (0, 0), (-1, -1), 0.4, colors.HexColor('#BBBBBB')),
        ('VALIGN', (0, 0), (-1, -1), 'MIDDLE'),
        ('TOPPADDING', (0, 0), (-1, -1), 4),
        ('BOTTOMPADDING', (0, 0), (-1, -1), 4),
        ('LEFTPADDING', (0, 0), (-1, -1), 5),
        ('RIGHTPADDING', (0, 0), (-1, -1), 5),
    ]
    if header:
        ts += [('BACKGROUND', (0, 0), (-1, 0), colors.HexColor('#0B5394')),
               ('TEXTCOLOR', (0, 0), (-1, 0), colors.white)]
    t.setStyle(TableStyle(ts))
    return t

story = []

# ===== 封面 =====
story.append(Spacer(1, 60*mm))
story.append(P('Wifitry 智能水族监测系统', 'title'))
story.append(P('操作手册', 'title'))
story.append(P('功能 · 使用 · 注意事项', 'subtitle'))
story.append(P('基于 ESP32-S3 · WiFi + MQTT · 手机 App · 语音助手', 'subtitle'))
story.append(PageBreak())

# ===== 一、系统概述 =====
story.append(P('一、系统概述', 'h1'))
story.append(P('Wifitry 是一套基于 ESP32-S3 的智能鱼缸（水族箱）监测与控制系统，可实时采集水质数据、远程控制加热棒/排水泵/补水泵/灯带等设备，并支持手机 App 和语音两种交互方式。', 'body'))
story.append(P('核心能力：', 'h2'))
for line in [
    '· 传感器：pH、水温(DS18B20)、TDS、双水位传感器；',
    '· 执行器：加热棒、排水泵、补水泵、RGB 灯带、蜂鸣器（均为继电器/高低电平控制）；',
    '· 通信：WiFi 联网（2.4GHz），通过公网 MQTT（EMQX 云）与手机 App 双向通信；',
    '· 语音：云端 AI 语音助手（小智）+ 本地离线 TTS 播报；',
    '· 自动化：低水位无水时自动补水，补到高水位自动停止；',
    '· 运维：pH 软件校准（单点/两点）、pH 显示钳位、USB 刷机（推荐）/OTA 无线升级。',
]:
    story.append(P(line, 'body'))

# ===== 二、功能介绍 =====
story.append(P('二、功能介绍', 'h1'))
story.append(P('2.1 传感器监测', 'h2'))
story.append(P('系统每 3 秒采集一次并上报到手机 App：', 'body'))
story.append(table([
    [P('传感器', 'cellc'), P('引脚', 'cellc'), P('说明', 'cell')],
    [P('pH 酸碱度', 'cellc'), P('GPIO4', 'cellc'), P('模拟量，支持软件校准，显示钳位 7.2±0.4', 'cell')],
    [P('水温', 'cellc'), P('GPIO5', 'cellc'), P('DS18B20 数字温度探头（需 4.7kΩ 上拉电阻），读取失败自动重试', 'cell')],
    [P('TDS 溶解固体', 'cellc'), P('GPIO7', 'cellc'), P('模拟量，按 DFRobot 公式换算 ppm', 'cell')],
    [P('低水位(1号)', 'cellc'), P('GPIO9', 'cellc'), P('数字量 NPN，0=有水 1=无水', 'cell')],
    [P('高水位(2号)', 'cellc'), P('GPIO38', 'cellc'), P('数字量 NPN，0=有水 1=无水', 'cell')],
], [40*mm, 28*mm, 120*mm]))
story.append(P('注：App 上还预留了「氨氮 / 硝酸盐 / 亚硝酸盐 / 溶氧」四个显示位，目前无对应传感器，均显示斜杠「/」。', 'body'))

story.append(P('2.2 设备控制', 'h2'))
story.append(table([
    [P('设备', 'cellc'), P('引脚', 'cellc'), P('控制方式', 'cell')],
    [P('加热棒', 'cellc'), P('GPIO8', 'cellc'), P('继电器，高电平触发（与补水泵对调后）', 'cell')],
    [P('排水泵', 'cellc'), P('GPIO12', 'cellc'), P('继电器，高电平触发，手动控制', 'cell')],
    [P('补水泵', 'cellc'), P('GPIO11', 'cellc'), P('继电器，高电平触发，一键/语音/自动水位控制', 'cell')],
    [P('RGB 灯带', 'cellc'), P('GPIO3', 'cellc'), P('WS2812，支持颜色/开关', 'cell')],
    [P('蜂鸣器', 'cellc'), P('GPIO10', 'cellc'), P('开关控制', 'cell')],
], [40*mm, 28*mm, 120*mm]))

story.append(P('2.3 语音交互', 'h2'))
for line in [
    '· 云端 AI 助手（小智）：通过 xiaozhi.me 服务，语音对话并可用 MCP 工具控制设备（读水质、开/关排水泵、开/关补水泵、开/关加热棒、开关灯）；',
    '· 本地 TTS：MQTT 发 TONE: 指令，设备离线合成语音播报水质报告；',
]:
    story.append(P(line, 'body'))

story.append(P('2.4 自动化与运维', 'h2'))
for line in [
    '· 根据水位自动补水（详见 4.7 节）：低水位传感器(1号)检测到「无水」→ 自动开补水泵；补到高水位传感器(2号)检测到「有水」→ 自动停；',
    '· pH 显示钳位：pH 读数被限制在 7.2±0.4（即 6.8~7.6）内，防止探头漂移导致读数爆表；',
    '· 固件更新：推荐 USB 刷机（idf.py -p COM口 flash），也支持 OTA 无线升级（双分区保护，失败不会变砖）。',
]:
    story.append(P(line, 'body'))

# ===== 三、硬件接线 =====
story.append(P('三、硬件接线', 'h1'))
story.append(P('各传感器/执行器与 ESP32-S3 的接线如下（务必共地）：', 'body'))
story.append(table([
    [P('模块', 'cellc'), P('模块引脚', 'cellc'), P('ESP32-S3', 'cellc'), P('备注', 'cell')],
    [P('DS18B20', 'cellc'), P('VDD / DQ / GND', 'cellc'), P('3.3V / GPIO5 / GND', 'cellc'), P('DQ 与 3.3V 间加 4.7kΩ 上拉', 'cell')],
    [P('XKC-Y21 水位(两个)', 'cellc'), P('黄(5V) 黑(OUT) 红(GND)', 'cellc'), P('5V / GPIO9 或 38 / GND', 'cellc'), P('NPN 输出，0=有水', 'cell')],
    [P('INMP441 麦克风', 'cellc'), P('VDD SD WS SCK', 'cellc'), P('3.3V / GPIO18 / GPIO14 / GPIO13', 'cellc'), P('L/R 接 GND 选左声道', 'cell')],
    [P('MAX98357A 功放', 'cellc'), P('VIN BCLK LRCK DIN', 'cellc'), P('5V / GPIO15 / GPIO16 / GPIO17', 'cellc'), P('接喇叭', 'cell')],
    [P('加热棒继电器', 'cellc'), P('IN', 'cellc'), P('GPIO8', 'cellc'), P('高电平触发；IN 建议加 10kΩ 下拉', 'cell')],
    [P('排水泵继电器', 'cellc'), P('IN', 'cellc'), P('GPIO12', 'cellc'), P('高电平触发；IN 建议加 10kΩ 下拉', 'cell')],
    [P('补水泵继电器', 'cellc'), P('IN', 'cellc'), P('GPIO11', 'cellc'), P('高电平触发；IN 建议加 10kΩ 下拉', 'cell')],
    [P('WS2812 灯带', 'cellc'), P('DIN', 'cellc'), P('GPIO3', 'cellc'), P('5V 供电，共地', 'cell')],
    [P('蜂鸣器', 'cellc'), P('I/O', 'cellc'), P('GPIO10', 'cellc'), P('有源蜂鸣器', 'cell')],
    [P('pH 传感器', 'cellc'), P('模拟输出', 'cellc'), P('GPIO4', 'cellc'), P('模拟量，软件校准', 'cell')],
    [P('TDS 传感器', 'cellc'), P('模拟输出', 'cellc'), P('GPIO7', 'cellc'), P('模拟量', 'cell')],
], [34*mm, 48*mm, 52*mm, 54*mm]))

# ===== 四、使用说明 =====
story.append(P('四、使用说明', 'h1'))

story.append(P('4.1 上电启动', 'h2'))
for line in [
    '1. 按上表接好所有硬件，确认供电正确；',
    '2. 给设备供 5V 电（建议 5V/2A 以上电源）；',
    '3. 设备自动连接 WiFi（SSID 与密码在固件的 secrets.h 中配置），约 20 秒连上；',
    '4. 连上后自动连接 MQTT 与语音服务器，开始每 3 秒上报一次数据。',
]:
    story.append(P(line, 'body'))

story.append(P('4.2 手机 App 使用', 'h2'))
for line in [
    '· 安装《鱼缸管家》App（Android APK，当前 v1.3.0）；',
    '· 打开 App 自动连接 MQTT，右上角显示「已连接」即成功；',
    '· 断线会自动重连：App 每 5 秒自检一次，断开时显示「已断开，自动重连中...」，恢复后自动变回「已连接」，无需手动点连接；',
    '· 界面顶部显示实时水质（pH/水温/TDS/低水位/高水位/自动补水状态），并预留氨氮/硝酸盐/亚硝酸盐/溶氧占位；',
    '· 下方为 pH、水温、TDS 三张历史折线图；',
    '· 底部按钮可远程控制（共 8 个）：灯带颜色（红/绿/蓝）、关灯、加热开/关、一键排水/排水关、一键补水/补水关、自动补水开关、播报水质；',
    '· 排水泵/补水泵按钮会跟随设备实际状态自动同步（收到 [PUMP]/[REFILL] 开/关 时更新）；',
    '· 「自动补水」状态色块：绿=待机、蓝=补水中、灰=已停用（超时保护或手动关掉）；',
    '· 设备产生告警时（补水泵超时、水位传感器异常等），页面顶部会弹出红色告警横幅；',
    '· 补水/水位类告警的横幅上带「一键恢复自动补水」按钮，点一下即可恢复，无需记指令。',
    '· 说明：蜂鸣器已从 App 界面移除（固件仍支持 BUZZ:1/0 指令与语音控制）。',
]:
    story.append(P(line, 'body'))

story.append(P('4.3 MQTT 指令表（App/任意 MQTT 工具可用）', 'h2'))
story.append(P('指令发往话题：esp32s3/chat/cmd；设备上报在：esp32s3/chat/log；broker 在固件 secrets.h 与 App secrets.dart 中配置（设备走 8883/TLS，App 走 8084/WebSocket/TLS）', 'body'))
story.append(table([
    [P('指令', 'cellc'), P('作用', 'cell')],
    [P('LED:R,G,B', 'cellc'), P('灯带设颜色，如 LED:255,0,0 = 红', 'cell')],
    [P('LEDOFF', 'cellc'), P('关闭灯带', 'cell')],
    [P('BUZZ:1 / BUZZ:0', 'cellc'), P('蜂鸣器 开/关', 'cell')],
    [P('HEAT:1 / HEAT:0', 'cellc'), P('加热棒 开/关', 'cell')],
    [P('PUMP:1 / PUMP:0', 'cellc'), P('排水泵 开/关（手动，无自动逻辑）', 'cell')],
    [P('REFILL:1 / REFILL:0', 'cellc'), P('补水泵 开/关，并同时启用/停用自动补水', 'cell')],
    [P('REFILLAUTO:1 / REFILLAUTO:0', 'cellc'), P('只启用/停用「根据水位自动补水」，不改变水泵当前状态', 'cell')],
    [P('TONE:', 'cellc'), P('本地语音播报当前水质（离线语音，不依赖网络）', 'cell')],
    [P('AI:文本', 'cellc'), P('把文本当作"用户说的话"发给小智，AI 会用语音回应（如 AI:你好）', 'cell')],
    [P('PHCALS:7.5', 'cellc'), P('pH 单点校准（探针放入已知 pH 液体后发）', 'cell')],
    [P('PHCAL:7 / PHCAL:4', 'cellc'), P('pH 两点校准（pH7.0 / pH4.0 缓冲液）', 'cell')],
    [P('OTA:http://ip:8000/Wifitry.bin', 'cellc'), P('远程 OTA 升级', 'cell')],
], [60*mm, 128*mm]))

story.append(P('4.4 语音控制', 'h2'))
for line in [
    '· 对设备说「打开灯带」「开补水泵」「关排水泵」「关加热棒」「看看水质」等，云端 AI 会调用工具执行；',
    '· 语音助手需在 xiaozhi.me 后台配置好人设（鱼缸管家提示词），否则只会聊天不会控制设备；',
    '· 补水泵语音指令「开补水泵」= 开始补水，补到高水位自动停；排水泵为手动开关。',
]:
    story.append(P(line, 'body'))

story.append(P('4.5 pH 校准', 'h2'))
for line in [
    '· 单点校准（临时，需已知 pH 的液体）：探针放入液体，发 PHCALS:7.5（换成实际 pH 值）；',
    '· 两点校准（推荐，更准）：探针放入 pH7.00 缓冲液发 PHCAL:7，再放入 pH4.00 缓冲液发 PHCAL:4；',
    '· 校准值自动存入 NVS，断电不丢；建议每隔数周重新校准一次；',
    '· 注意：当前固件将 pH 显示钳位在 7.2±0.4（6.8~7.6）内，超出该范围的真实值会被钉在边界，属有意为之的显示保护。',
]:
    story.append(P(line, 'body'))

story.append(P('4.6 固件更新', 'h2'))
story.append(P('方式一：一键刷机包（推荐，客户可自行操作）', 'body'))
for line in [
    '· 工具包内容：esptool.exe（免安装，不需要 Python / ESP-IDF / Arduino）、Wifitry.bin、'
    'bootloader.bin、partition-table.bin、ota_data_initial.bin、刷机.bat、刷机说明.txt；',
    '· 使用步骤：',
    '    1) 把工具包解压到电脑上任意文件夹（不要在压缩包内直接双击）；',
    '    2) 用 USB 数据线把设备连到电脑，并拔掉麦克风（USB 与麦克风不能同时接）；',
    '    3) 双击「刷机.bat」→ 按任意键开始 → 等待约 30 秒；',
    '    4) 屏幕出现「FLASH OK !」即成功，设备会自动重启；',
    '    5) 拔掉 USB、接回麦克风，对着设备说「播报水质」验证。',
    '· 运行环境要求：Windows 10/11 64 位；无需安装任何软件；',
    '· 若提示自动识别串口失败，输入设备管理器里看到的串口号（如 COM5）即可；',
    '· 刷机不会清除 WiFi 配置、小智激活信息、pH 校准值和本地语音包。',
]:
    story.append(P(line, 'body'))
story.append(P('方式二：USB 刷机（开发/维护用）', 'body'))
for line in [
    '1. 用 USB 数据线连接电脑（刷机时请先断开麦克风）；',
    '2. 在电脑上执行：idf.py -p COM口 flash；',
    '3. 刷完自动重启，重新连接 WiFi 与 MQTT。',
]:
    story.append(P(line, 'body'))
story.append(P('方式三：OTA 无线升级（备用）', 'body'))
for line in [
    '1. 电脑上把新固件 Wifitry.bin 放入某文件夹；',
    '2. 该文件夹内启动 HTTP 服务器：python -m http.server 8000；',
    '3. 查电脑局域网 IP，确认与设备同网段；',
    '4. 发指令：OTA:http://电脑IP:8000/Wifitry.bin；',
    '5. 设备自动下载、烧写、重启（有双分区保护，失败不会变砖）；近期 OTA 写入偶有卡死，优先用前两种方式。',
]:
    story.append(P(line, 'body'))
story.append(P('一键刷机常见问题：', 'body'))
story.append(table([
    [P('现象', 'cellc'), P('处理', 'cell')],
    [P('设备管理器里找不到 COM 口', 'cellc'), P('换一根 USB 数据线（很多线只能充电不能传数据）；确认设备已通电', 'cell')],
    [P('双击刷机.bat 窗口一闪就没了', 'cellc'), P('解压不完整（缺文件），请把 zip 完整解压后，在解压出来的文件夹里运行', 'cell')],
    [P('杀毒软件报警 / Windows 提示「已阻止」', 'cellc'), P('esptool.exe 是打包工具，常被误报；点「仍要运行」或把整个文件夹加入信任名单', 'cell')],
    [P('提示串口被占用 / could not open port', 'cellc'), P('关闭其他占用串口的程序（串口助手、Arduino IDE 等）后重试', 'cell')],
    [P('刷到一半中断', 'cellc'), P('直接重新双击「刷机.bat」再刷一次即可，不会刷坏', 'cell')],
], [52*mm, 116*mm]))

story.append(P('4.7 根据水位自动补水', 'h2'))
story.append(P('水位传感器约定：1 号 GPIO9 = 低水位，2 号 GPIO38 = 高水位；均为 NPN 输出，0=有水(浸没)、1=无水(露出)。', 'body'))
story.append(P('补水策略（两段式 + 迟滞，把水补到最高点）：', 'body'))
for line in [
    '· 低水位「无水」 → 开始补水；',
    '· 高水位「有水」 → 停止补水（水位已到最高点）；',
    '· 水位在两个探头之间 → 保持现状（避免临界点反复启停）。',
]:
    story.append(P(line, 'body'))
story.append(P('水位读数滤波（滑窗 + 不对称判定，兼顾灵敏与稳定）：', 'body'))
story.append(P('传感器是数字量，且实际使用中容易受干扰、读数跳变。固件以 50ms 周期连续采样，'
               '对最近 7 个样本做表决，判定门槛故意做成不对称：', 'body'))
story.append(table([
    [P('判定', 'cellc'), P('门槛', 'cell'), P('目的', 'cellc')],
    [P('判「有水」', 'cellc'), P('7 个样本里 ≥3 个是"有水"', 'cell'), P('灵敏，约 0.15~0.35 秒响应', 'cell')],
    [P('判「无水」', 'cellc'), P('7 个样本里 ≤1 个是"有水"', 'cell'), P('稳，单个毛刺不会误判', 'cell')],
    [P('临界区', 'cellc'), P('2 个"有水"', 'cell'), P('保持原状态（迟滞，不来回跳）', 'cell')],
], [26*mm, 66*mm, 76*mm]))
story.append(P('三种状态（App 上以色块显示）：', 'body'))
story.append(table([
    [P('状态', 'cellc'), P('含义', 'cell'), P('App 显示', 'cellc')],
    [P('待机', 'cellc'), P('水位正常，无需补水', 'cell'), P('绿色', 'cellc')],
    [P('补水中', 'cellc'), P('正在补水', 'cell'), P('蓝色', 'cellc')],
    [P('已停用', 'cellc'), P('自动补水被关掉（手动关过，或超时保护触发）', 'cell'), P('灰色', 'cellc')],
], [30*mm, 108*mm, 30*mm]))
story.append(P('五重安全保护与恢复方式（不需要记特殊指令）：', 'body'))
story.append(table([
    [P('#', 'cellc'), P('保护', 'cell'), P('怎么恢复', 'cell')],
    [P('1', 'cellc'), P('上电 15 秒宽限：等水位读数稳定，不会一上电就开始补水', 'cell'), P('自动', 'cellc')],
    [P('2', 'cellc'), P('单次补水最长 5 分钟：超时强制停泵，并自动停用自动补水', 'cell'), P('App 点「自动补水:开」一键恢复', 'cell')],
    [P('3', 'cellc'), P('读数矛盾检测：低水位无水 + 高水位有水（物理上不可能）→ 停泵 + 告警', 'cell'), P('读数恢复正常后自动继续', 'cellc')],
    [P('4', 'cellc'), P('手动关泵（REFILL:0）会同时停用自动补水，保证「关得掉」', 'cell'), P('发 REFILL:1 或 REFILLAUTO:1', 'cell')],
    [P('5', 'cellc'), P('手动优先期：手动/语音操作泵之后 10 分钟内，自动水位逻辑不干预，'
                        '避免"刚开就被自动关掉"', 'cell'), P('到期后自动恢复自动控制', 'cellc')],
], [12*mm, 106*mm, 50*mm]))
story.append(P('排水泵（GPIO12）为纯手动控制，没有自动逻辑。补水泵超时或水位异常时，App 顶部会弹出红色告警横幅，'
               '横幅右下角带「一键恢复自动补水」按钮，点一下即可恢复。', 'body'))
story.append(P('排水泵（GPIO12）为纯手动控制，没有自动逻辑。补水泵超时或水位异常时，App 顶部会弹出红色告警横幅，'
               '横幅右下角带「一键恢复自动补水」按钮，点一下即可恢复。', 'body'))

# ===== 五、注意事项 =====
story.append(P('五、注意事项', 'h1'))
story.append(P('5.1 供电', 'h2'))
for line in [
    '· 用 5V/2A 以上的稳定电源，避免电脑 USB 口直接供电；',
    '· 电源纹波要小（建议 <50mV）；若 5V 纹波过大（如 0.5V）会导致 WiFi 掉线、重启、传感器读数乱跳；',
    '· 5V 入口加 100~470µF 电解电容，3.3V 靠近芯片加 100µF + 1µF 陶瓷电容；',
    '· 电源线要粗、短，勿用细杜邦线当电源线。',
]:
    story.append(P(line, 'body'))

story.append(P('5.2 继电器与安全', 'h2'))
for line in [
    '· 加热棒、排水泵、补水泵都用继电器，高电平触发；',
    '· 继电器 IN 脚对地加 10kΩ 下拉，防止上电瞬间误吸合；',
    '· 加热棒严禁干烧，水泵严禁无水空转；',
    '· 涉及 220V/大电流的设备，务必做好绝缘与防水。',
]:
    story.append(P(line, 'body'))

story.append(P('5.3 传感器', 'h2'))
for line in [
    '· DS18B20 的 DQ 与 3.3V 之间必须接 4.7kΩ 上拉电阻，否则读不到；读取失败会自动重试，接好即恢复，无需重启；',
    '· 水位传感器 XKC-Y21 是 NPN 输出：0=有水、1=无水，供电接 5V（勿接 3.3V）；',
    '· INMP441 麦克风供电 3.3V（勿接 5V），MAX98357A 功放供电 5V；',
    '· pH 探针属高阻抗电极，接线尽量短、远离强电与水泵线，读数受电源纹波影响大；',
    '· pH 探针不用时应浸入保护液（3M KCl），避免长期干放损坏。',
]:
    story.append(P(line, 'body'))

story.append(P('5.4 网络', 'h2'))
for line in [
    '· ESP32-S3 仅支持 2.4GHz WiFi，请确认路由器开启 2.4G 频段；WiFi 名称与密码在固件 secrets.h 中配置；',
    '· 设备与手机 App 都通过公网 MQTT（EMQX 云）通信，手机无需与设备同一 WiFi，只要手机能联网即可；',
    '· OTA 远程升级时，电脑与设备需在同一局域网。',
]:
    story.append(P(line, 'body'))

story.append(P('5.5 语音助手', 'h2'))
for line in [
    '· 语音识别依赖云端服务，说话请放慢、吐字清晰；',
    '· 语音控制设备需在 xiaozhi.me 后台为设备配置「鱼缸管家」人设并启用工具调用；',
    '· 麦克风已做噪声门 + 自动增益（阈值 500，最多 96 倍放大），小声说话也能识别；',
    '· 云端 AI 断网时会自动重连，本地 MQTT 控制与 TTS 播报不受语音服务器影响。',
]:
    story.append(P(line, 'body'))

story.append(P('5.6 水位传感器排障（重要）', 'h2'))
story.append(P('设备上报里带有【原始水位】字段，它是未经滤波的瞬时读数，用来区分"软件"还是"硬件"问题：', 'body'))
story.append(P('pH=7.44 水温=22.3 C TDS=22ppm 水位1=1 水位2=1 原始水位=0/1 补水状态=已停用', 'code'))
story.append(table([
    [P('把水位探头从水里拿出/放入时', 'cellc'), P('结论与处理', 'cell')],
    [P('原始水位会跟着 0↔1 变化', 'cellc'), P('传感器与接线正常，属软件滤波问题，可反馈给我们调整参数', 'cell')],
    [P('原始水位始终不变', 'cellc'), P('硬件/安装问题，软件无法解决，按下面几条排查：', 'cell')],
], [50*mm, 118*mm]))
for line in [
    '· 探头必须【紧贴容器外壁】，中间不能有空气间隙（可用胶/扎带压紧）；',
    '· 容器壁不宜过厚，一般 ≤3mm 效果最好；非接触式电容传感器对壁厚敏感；',
    '· 供电必须接 5V（XKC-Y21 不工作在 3.3V），且与主板共地；',
    '· 信号线建议对 5V 加 10kΩ 上拉电阻：传感器是 NPN 开集输出，'
    '固件的内部上拉只有约 45kΩ，线一长就容易不稳；',
    '· 探头位置：低水位探头放在"需要开始补水"的高度，高水位探头放在"补满即停"的高度，两者之间留出余量。',
]:
    story.append(P(line, 'body'))

# ===== 结尾 =====
story.append(Spacer(1, 10*mm))
story.append(P('—— 本手册随 App v1.3.0 / 固件(水位滤波+手动优先版) 生成 ——', 'subtitle'))

doc = SimpleDocTemplate(OUT, pagesize=A4,
                        leftMargin=18*mm, rightMargin=18*mm,
                        topMargin=16*mm, bottomMargin=16*mm,
                        title='Wifitry 智能水族监测系统 操作手册')
doc.build(story)
print('已生成:', OUT)

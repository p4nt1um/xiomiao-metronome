# 小喵掌机节拍器（xiomiao-metronome）

运行在**学而思小喵掌机**（ESP32-WROVER-B）上的中文界面音乐节拍器，
供钢琴等乐器练习打拍子。功能对标主流在线节拍器（rtcd.io / musicca / metronome-online），
并内置面向练习的进阶功能：每拍细分、静音小节训练、练习计时、点击测速。

> 硬件资料与驱动来源：[xueersi-idf](https://github.com/ZyoungInc/xueersi-idf)
> （本工程的屏幕 / 按键 / 蜂鸣器驱动复用自该工程，按其要求在此署名引用，感谢 ZYoungInc 用爱发电）

## 功能

### 核心

- **BPM 10–250**：上/下键 ±1（长按连发加速）、左/右键 ±10
- **速度术语**：13 档意大利文实时显示（Larghissimo ≤20 / Grave 21-40 / Lento 41-45 /
  Largo 46-50 / Adagio 51-60 / Adagietto 61-70 / Andante 71-85 / Moderato 86-97 /
  Allegretto 98-109 / Allegro 110-132 / Vivace 133-140 / Presto 141-177 / Prestissimo ≥178）
- **拍号**：每小节 1–9 拍（覆盖 1、2/4、3/4、4/4、5/4、6/8、7/8、8/8、9/8，
  含华尔兹 3/4、进行曲 2/4、复合拍 6/8 等常见舞曲）
- **重音**：第一拍强拍高音（1568Hz），其余弱拍（880Hz），可开关
- **音量**：0–8 级（0 静音），调节时即时试听
- **拍点可视化**：大号圆点横排于 BPM 右侧，当前拍放大高亮；
  1–4 拍单行 13px、5–6 拍单行 11px、7–9 拍折两行 10px
- **掉电记忆**：全部设置存 NVS，重启恢复

### 练习

- **每拍细分**：四分 / 八分 / 三连音 / 十六分（细分拍为低音弱短音，运行中平滑切换）
- **静音小节训练**：播 N 小节（1–8）→ 静 M 小节（0–8）循环；静音段无声但拍点变暗仍走动，
  状态栏显示「静音中」，锻炼内心节拍感
- **练习计时**：关 / 5 / 10 / 15 / 20 / 30 / 45 / 60 分钟倒计时（主界面右上角 mm:ss），
  到时自动停止 + 三声提示音
- **点击测速**：A 键连敲，实时 BPM 取最近 8 次间隔均值；异常间隔（>2 秒或 <120ms）自动丢弃；
  2 秒无敲击自动返回并应用
- **光控暂停**：光照传感器（GPIO36）**遮一下切换**——遮住约 0.6 秒 = 暂停/恢复切换，
  松开无动作，开灯/拿开手不误触；暂停立即静音、状态栏「已暂停」、低音一声反馈，
  恢复从第 1 拍强拍起步，练习计时同步冻结；设置页可开关（默认开）
- **恢复默认**：设置页 A 键长按 1 秒确认

## 按键表

| 界面 | 按键 | 功能 |
|---|---|---|
| 主界面 | ▲ / ▼ | BPM ±1（长按连发） |
| 主界面 | ◀ / ▶ | BPM ±10 |
| 主界面 | A | 启动 / 停止；暂停态下 = 恢复 |
| 主界面 | 遮一下光照传感器 | 运行 ↔ 暂停 切换（约 0.6 秒确认，可在设置关闭） |
| 主界面 | B | 进入设置 |
| 设置页 | ▲ / ▼ | 选择行（8 行滚动列表） |
| 设置页 | ◀ / ▶ | 调整当前行数值 |
| 设置页 | A | 静音小节：切换 播/静 字段；点击测速：进入；恢复默认：长按 1 秒 |
| 设置页 | B | 返回主界面 |
| 测速页 | A | 敲击 |
| 测速页 | B | 返回并应用 |

## 时序精度

节拍由 `esp_timer` 周期定时驱动（绝对时间对齐，无累积漂移），
改 BPM/细分用 `esp_timer_restart` 平滑切换。拍点在 esp_timer 回调内取
`esp_timer_get_time()` 时间戳并经串口输出（`metro_eng: beat=.. ts=..`）。

实测（120 BPM，静音模式，连续运行 6.4 分钟 / 764 个拍间隔，`tools/analyze_precision.py` 统计）：

| 指标 | 结果 |
|---|---|
| 平均间隔误差 | +0.2 µs（理论 500000 µs） |
| 间隔标准差 | 6.4 µs |
| 单拍最大偏差 | 177 µs（仅启动后第一个间隔；稳态抖动 ±2 µs 以内） |
| 累积漂移 | +175 µs / 382 s ≈ 0.23 µs/拍（无累积漂移） |

远优于人耳可辨的 ±2 ms 目标。

## 工程结构

```
main/
├── main.c            # app_main：初始化 bsp/设置/引擎，LVGL 任务（事件泵 + 渲染）
├── bsp_lcd.c/.h      # ST7735 160x128 横屏 SPI 驱动 + LVGL 双缓冲接入
├── bsp_buttons.c/.h  # 6 键轮询 + 25ms 消抖 + LVGL keypad indev
├── bsp_buzzer.c/.h   # GPIO14 无源蜂鸣器 LEDC：tone(freq, duty, duration)
├── bsp_light.c/.h    # 光照传感器 ADC（GPIO36 = ADC1_CH0）
├── light_ctl.c/.h    # 光照突变检测状态机（遮一下切换手势）
├── metro_engine.c/.h # 节拍引擎：定时、拍型推进、细分、静音小节、暂停三态
├── app_settings.c/.h # 设置持久化（NVS + 800ms 防抖落盘）
├── ui.c/.h           # 主界面 / 设置页 / 测速页（页面切换、按键分发、倒计时）
└── fonts/            # 中文字体子集（lv_font_conv 生成）
tools/
├── idf_cmd.bat       # 一键激活 ESP-IDF 环境并执行 idf.py（规避 Git Bash/编码坑）
├── gen_fonts.js      # 中文字体再生成脚本（simhei + Segoe UI Symbol 双源合并）
├── charset.txt       # 字体字符集
└── read_serial.py    # 免 monitor 抓串口日志（含 DTR/RTS 处理）
```

## 构建与烧录

环境：ESP-IDF v5.5.2（`E:\Espressif\frameworks\esp-idf-v5.5.2`），设备串口 **COM8**。

```bash
# 方式一：包装脚本（自动激活环境，推荐，Git Bash / cmd 均可）
cmd /c tools\idf_cmd.bat build
cmd /c tools\idf_cmd.bat -p COM8 -b 460800 flash
cmd /c tools\idf_cmd.bat -p COM8 monitor

# 方式二：手动激活
E:\Espressif\frameworks\esp-idf-v5.5.2\export.bat
idf.py build
idf.py -p COM8 -b 460800 flash monitor

# 中文字体再生成（修改 tools/charset.txt 后）
cd tools && npm install && node gen_fonts.js
```

### 串口注意事项（GD32 USB 桥接）

- 直接用 pyserial 开串口若 DTR/RTS 电平不当，会把板子带进 ROM 下载模式（`waiting for download`）
- `idf.py monitor` 不要加 `--no-reset`，让 monitor 用正确复位序列重启进应用
- 需要脚本抓日志时用 `tools/read_serial.py`（DTR/RTS 时序已按 esptool 经典电路调好）

### 已知硬件限制（来自 xueersi-idf 实测）

- 屏幕 TE 引脚未接 MCU，无法垂直同步（快速全屏刷新可能撕裂）；本应用局部刷新，影响可忽略
- 背光引脚直连电源，无法调节亮度
- GPIO12（B 键）是 boot 敏感脚

## 开发文档

需求归纳、硬件复用清单、UI 原型、里程碑与验收记录见 [开发方案.md](开发方案.md)。

## 开源致谢

本工程复用了 [xueersi-idf](https://github.com/ZyoungInc/xueersi-idf)（作者 ZYoungInc）
的硬件驱动代码与配置，按原作者要求署名引用：二次开发请一并遵守其署名要求。

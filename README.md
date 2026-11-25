# Flip Clock (ESP32-P4 · Guition JC4880P443C)

横屏翻页时钟 + 闹钟 + 天气 + USB 遥控，跑在 ESP32-P4（4.3" 480×800，ES8311，GT911）上。

## 功能

- **翻页时钟**：800×480 横屏（软件旋转），深灰卡片、白色数字、铰链细节、上下翻页动画
- **底部信息**：日期 · 星期 · 农历（含闰月）· 天气；右上角 ALARM + 网络状态
- **触摸界面**：点空白进 INFO 页（时间/日期/天气/位置/闹钟/网络），可设置闹钟、手动对时、Wi-Fi 扫描配网、城市选择（35 个常用城市）
- **闹钟**：到点蜂鸣 + 全屏提示，点屏停止；设置保存到 NVS
- **联网**：ESP32-C6（esp_hosted / esp_wifi_remote）Wi-Fi + SNTP 自动对时；Open-Meteo 天气（可选按 IP 定位）
- **USB 遥控**：电脑通过 USB 串口看屏并点击控制，不依赖 Wi-Fi（Base64 分帧 JPEG，320×533 @ ~8fps）

## 硬件

| 项 | 说明 |
|---|---|
| 主控 | ESP32-P4（rev 3.x，双核 400MHz，32MB PSRAM，16MB flash） |
| 屏幕 | 4.3" 480×800 ST7701S，MIPI-DSI 2 lane |
| 触摸 | GT911（I2C 0x5D，复位 GPIO3，INT 不用） |
| 音频 | ES8311 + NS4150，I2S 13/12/10/9，功放使能 GPIO11 |
| 无线 | ESP32-C6（SDIO：CLK18 CMD19 D0-3 14-17，复位 54） |

## 编译

需要 ESP-IDF **v6.0.2**：

```bash
. ~/esp/esp-idf/export.sh
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/cu.usbmodem101 flash
```

## 串口命令（USB-Serial/JTAG，115200）

| 命令 | 作用 |
|---|---|
| `T<epoch>` / `t` | 设置/打印时间 |
| `A<HH:MM>` / `A0` / `A?` | 设置/取消/查看闹钟 |
| `W<ssid>,<pass>` | 设置 Wi-Fi |
| `P<lat>,<lon>` / `G` / `L` | 手动坐标 / 按 IP 定位 / 查看位置 |
| `B<0-100>` | 亮度 |
| `M1` / `M0` | 开启/关闭 USB 镜像 |
| `TAP x y` | 注入一次点击（原生 480×800 坐标） |
| `S` | 开关 HTTP 看屏服务 |
| `R` | 面板重新同步（修复绿屏） |

## Mac 遥控工具

`tools/remote_usb.py`（需要 pyserial + Pillow）：

```bash
python3 tools/remote_usb.py /dev/cu.usbmodem101
```

## 踩坑记录

- GT911 复位脚是 **GPIO3**，且 `int_gpio_num` 必须为 -1（轮询），否则 LVGL 收不到触摸
- 整屏重绘时 `lv_draw_add_task` 崩溃 → LVGL 内存池加到 128KB、任务栈 16KB
- JPEG 编码器缓冲区必须用 `jpeg_alloc_encoder_mem` 分配（对齐）
- 串口控制台不能传二进制，镜像用 Base64 文本分帧（`#IMG` … `#END`）
- 截图直接从 DPI 帧缓冲读会干扰显示，需先下采样到私有缓冲

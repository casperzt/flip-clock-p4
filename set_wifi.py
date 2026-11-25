#!/usr/bin/env python3
"""给板子设置 Wi-Fi（保存到 NVS，之后开机自动连接并 SNTP 对时）。

用法:
    ~/.espressif/python_env/idf6.0_py3.12_env/bin/python set_wifi.py "SSID" "密码"

只支持 2.4GHz 网络（ESP32-C6 不带 5GHz）。
"""
import sys
import time

import serial

if len(sys.argv) < 3:
    print(__doc__)
    sys.exit(1)

ssid, password = sys.argv[1], sys.argv[2]
port = sys.argv[3] if len(sys.argv) > 3 else "/dev/cu.usbmodem101"

with serial.Serial(port, 115200, timeout=1) as s:
    time.sleep(0.2)
    s.write(f"W{ssid},{password}\n".encode())
    time.sleep(1.5)
    s.write(b"w\n")
    time.sleep(0.6)
    sys.stdout.write(s.read(16384).decode("utf-8", "replace"))

print(f"wifi config sent: {ssid}")

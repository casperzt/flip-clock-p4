#!/usr/bin/env python3
"""把 Mac 的当前时间发给板子（USB 串口）。

用法:
    ~/.espressif/python_env/idf6.0_py3.12_env/bin/python sync_time.py [/dev/cu.usbmodem101]

之后可以放进 cron 每分钟同步一次。
"""
import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem101"
epoch = int(time.time())

with serial.Serial(port, 115200, timeout=1) as s:
    time.sleep(0.2)
    s.write(f"T{epoch}\n".encode())
    time.sleep(0.2)
    s.write(b"t\n")
    time.sleep(0.4)
    data = s.read(4096)
    sys.stdout.write(data.decode("utf-8", "replace"))

print(f"sent T{epoch}")

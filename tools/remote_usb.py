#!/usr/bin/env python3
"""Mac 遥控工具（USB 串口版，不走 Wi-Fi）

- 画面：设备通过 USB 串口发送 Base64 分帧的 JPEG（#IMG ... #END）
- 点击：发送 "TAP x y" 到设备，等效手指点屏
用法: python3 remote_control.py [/dev/cu.usbmodem101]
"""
import base64
import re
import sys
import threading
import time
import tkinter as tk

import serial
from PIL import Image, ImageTk

LCD_W, LCD_H = 480, 800


class Remote(tk.Tk):
    def __init__(self, port):
        super().__init__()
        self.title("Flip Clock Remote (USB)")
        self.geometry("560x900")
        self.configure(bg="#0a0b0c")
        self.port = tk.StringVar(value=port)
        self.rotation = 270
        self.photo = None
        self.frame = None
        self.ser = None
        self.buf = ""
        self.running = True
        self.last_size = (0, 0)
        self.latency = 0.0
        self.status = tk.StringVar(value="connecting...")

        top = tk.Frame(self, bg="#0a0b0c")
        top.pack(fill="x", padx=8, pady=6)
        tk.Label(top, text="PORT", fg="#888", bg="#0a0b0c").pack(side="left")
        tk.Entry(top, textvariable=self.port, width=22).pack(side="left", padx=4)
        tk.Button(top, text="连接", command=self.connect).pack(side="left", padx=2)
        tk.Button(top, text="旋转", command=self.rotate).pack(side="left", padx=2)

        self.image_label = tk.Label(self, bg="#0a0b0c", bd=0, highlightthickness=0)
        self.image_label.pack(expand=True, fill="both")
        self.image_label.bind("<Button-1>", self.on_click)

        tk.Label(self, textvariable=self.status, fg="#6a737d", bg="#0a0b0c",
                 anchor="w").pack(fill="x", padx=8, pady=4)

        self.protocol("WM_DELETE_WINDOW", self.on_close)
        self.thread = threading.Thread(target=self.reader, daemon=True)
        self.thread.start()
        self.after(50, self.refresh_ui)
        self.connect()

    de = None  # placeholder to keep class attrs tidy

    def connect(self):
        try:
            if self.ser:
                self.ser.close()
            self.ser = serial.Serial(self.port.get().strip(), 115200, timeout=0.2)
            time.sleep(0.3)
            self.ser.reset_input_buffer()
            self.ser.write(b"M1\n")
            self.status.set("connected, waiting frames...")
        except Exception as e:
            self.ser = None
            self.status.set(f"open failed: {e}")

    def rotate(self, delta=90):
        self.rotation = (self.rotation + delta) % 360
        self.status.set(f"rotation {self.rotation}°")

    def reader(self):
        while self.running:
            if not self.ser:
                time.sleep(0.3)
                continue
            try:
                data = self.ser.read(4096)
            except Exception:
                time.sleep(0.3)
                continue
            if not data:
                continue
            self.buf += data.decode("utf-8", "replace")
            while True:
                m = re.search(r"#IMG (\d+)\r?\n", self.buf)
                if not m:
                    if len(self.buf) > 300000:
                        self.buf = self.buf[-200000:]
                    break
                start = m.end()
                end = self.buf.find("#END", start)
                if end < 0:
                    break
                chunk = self.buf[start:end]
                self.buf = self.buf[end + 4:]
                b64 = "".join(l for l in chunk.splitlines() if l.strip() and not l.startswith("#"))
                try:
                    self.frame = base64.b64decode(b64)
                    self.latency = 0
                except Exception:
                    pass

    def refresh_ui(self):
        if self.frame:
            try:
                img = Image.open(__import__("io").BytesIO(self.frame)).convert("RGB")
                img = img.rotate(self.rotation, expand=True)
                max_w = max(320, self.winfo_width() - 20)
                max_h = max(320, self.winfo_height() - 90)
                s = min(max_w / img.width, max_h / img.height)
                size = (int(img.width * s), int(img.height * s))
                img = img.resize(size, Image.LANCZOS)
                self.last_size = size
                self.photo = ImageTk.PhotoImage(img)
                self.image_label.config(image=self.photo)
                if self.status.get().startswith(("connected", "waiting")):
                    self.status.set(f"{self.port.get()}  live  {size[0]}x{size[1]}  点画面=点屏")
            except Exception as e:
                self.status.set(f"decode failed: {e}")
        self.after(80, self.refresh_ui)

    def on_click(self, event):
        w, h = self.last_size
        if w == 0 or h == 0:
            return
        dx = (event.x - (self.image_label.winfo_width() - w) / 2) / w
        dy = (event.y - (self.image_label.winfo_height() - h) / 2) / h
        if not (0 <= dx <= 1 and 0 <= dy <= 1):
            return
        r = self.rotation
        if r == 0:
            x, y = dx * LCD_W, dy * LCD_H
        elif r == 90:
            x, y = (1 - dy) * LCD_W, dx * LCD_H
        elif r == 180:
            x, y = (1 - dx) * LCD_W, (1 - dy) * LCD_H
        else:
            x, y = dy * LCD_W, (1 - dx) * LCD_H
        try:
            if self.ser:
                self.ser.write(f"TAP {int(x)} {int(y)}\n".encode())
        except Exception as e:
            self.status.set(f"tap failed: {e}")

    def on_close(self):
        self.running = False
        try:
            if self.ser:
                self.ser.write(b"M0\n")
                self.ser.close()
        except Exception:
            pass
        self.destroy()


if __name__ == "__main__":
    port = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem101"
    Remote(port).mainloop()

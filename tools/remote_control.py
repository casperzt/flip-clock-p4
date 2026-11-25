#!/usr/bin/env python3
"""Mac 端遥控工具：显示翻转时钟画面、点击等于点屏、开关推流。

用法:
    python3 remote_control.py [设备IP]
"""
import io
import sys
import threading
import time
import tkinter as tk
from urllib.parse import urlencode
from urllib.request import urlopen

from PIL import Image, ImageTk

LCD_W, LCD_H = 480, 800


class Remote(tk.Tk):
    def __init__(self, ip):
        super().__init__()
        self.title("Flip Clock Remote")
        self.geometry("560x900")
        self.configure(bg="#0a0b0c")
        self.ip = tk.StringVar(value=ip)
        self.rotation = 90          # PIL 角度（逆时针）
        self.photo = None
        self.frame = None
        self.running = True
        self.last_size = (0, 0)
        self.status = tk.StringVar(value="connecting...")

        top = tk.Frame(self, bg="#0a0b0c")
        top.pack(fill="x", padx=8, pady=6)

        tk.Label(top, text="IP", fg="#888", bg="#0a0b0c").pack(side="left")
        tk.Entry(top, textvariable=self.ip, width=16).pack(side="left", padx=4)
        tk.Button(top, text="连接", command=self.reconnect).pack(side="left", padx=2)
        tk.Button(top, text="旋转", command=self.rotate).pack(side="left", padx=2)
        self.stream_on = True
        self.stream_btn = tk.Button(top, text="看屏: ON", command=self.toggle_stream)
        self.stream_btn.pack(side="left", padx=2)

        self.image_label = tk.Label(self, bg="#0a0b0c", bd=0, highlightthickness=0)
        self.image_label.pack(expand=True, fill="both")
        self.image_label.bind("<Button-1>", self.on_click)

        tk.Label(self, textvariable=self.status, fg="#6a737d", bg="#0a0b0c",
                 anchor="w").pack(fill="x", padx=8, pady=4)

        # 启动时自动打开设备的看屏服务
        try:
            urlopen(f"{self.base()}/stream?on=1", timeout=3).read()
        except Exception:
            pass

        self.protocol("WM_DELETE_WINDOW", self.on_close)
        self.thread = threading.Thread(target=self.poll_loop, daemon=True)
        self.thread.start()
        self.after(50, self.refresh_ui)

    # ---------- 网络 ----------
    def base(self):
        return f"http://{self.ip.get().strip()}"

    def toggle_stream(self):
        self.stream_on = not self.stream_on
        try:
            urlopen(f"{self.base()}/stream?on={1 if self.stream_on else 0}", timeout=3).read()
            self.stream_btn.config(text="看屏: ON" if self.stream_on else "看屏: OFF")
        except Exception as e:
            self.stream_on = not self.stream_on
            self.status.set(f"推流开关失败: {e}")

    def reconnect(self):
        self.frame = None
        self.status.set("reconnecting...")

    def rotate(self, delta=90):
        self.rotation = (self.rotation + delta) % 360
        self.status.set(f"rotation {self.rotation}°")

    # ---------- 抓帧 ----------
    def poll_loop(self):
        last = 0
        while self.running:
            try:
                t0 = time.time()
                data = urlopen(f"{self.base()}/snap", timeout=4).read()
                if data[:2] == b"\xff\xd8":
                    self.frame = data
                    self.latency = (time.time() - t0) * 1000
                else:
                    self.status.set("device stream is off (点看屏 ON)")
            except Exception as e:
                self.status.set(f"offline: {e}")
            dt = time.time() - last
            last = time.time()
            time.sleep(max(0.0, 0.25 - dt))

    # ---------- 显示 ----------
    def refresh_ui(self):
        if self.frame is not None:
            try:
                img = Image.open(io.BytesIO(self.frame)).convert("RGB")
            except Exception:
                img = None
            if img is not None:
                img = img.rotate(self.rotation, expand=True)

                max_w = max(320, self.winfo_width() - 20)
                max_h = max(320, self.winfo_height() - 90)
                scale = min(max_w / img.width, max_h / img.height)
                size = (int(img.width * scale), int(img.height * scale))
                img = img.resize(size, Image.LANCZOS)
                self.last_size = size
                self.photo = ImageTk.PhotoImage(img)
                self.image_label.config(image=self.photo)
                self.status.set(f"{self.ip.get()}  {size[0]}x{size[1]}  "
                                f"{getattr(self, 'latency', 0):.0f}ms  点画面=点屏")
        self.after(120, self.refresh_ui)

    # ---------- 点击 ----------
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
        elif r == 90:          # PIL 逆时针 90
            x, y = (1 - dy) * LCD_W, dx * LCD_H
        elif r == 180:
            x, y = (1 - dx) * LCD_W, (1 - dy) * LCD_H
        else:
            x, y = dy * LCD_W, (1 - dx) * LCD_H
        try:
            urlopen(f"{self.base()}/tap?{urlencode({'x': int(x), 'y': int(y)})}", timeout=2).read()
        except Exception as e:
            self.status.set(f"tap failed: {e}")

    def on_close(self):
        self.running = False
        self.destroy()


if __name__ == "__main__":
    ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.8.161"
    Remote(ip).mainloop()

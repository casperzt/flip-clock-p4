#pragma once

#include <stdbool.h>

/* 启动网页看屏服务（http://<设备IP>/） */
void screen_stream_start(void);

/* 开关（INFO 页面用） */
void screen_stream_set_enabled(bool on);
bool screen_stream_enabled(void);

/* USB 串口镜像（不依赖 Wi-Fi） */
void screen_mirror_set(bool on);
void screen_tap(int x, int y);

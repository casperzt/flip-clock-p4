#pragma once

#include "lvgl.h"

/* 创建两个隐藏的设置面板（Wi-Fi、时间），需在 LVGL 初始化后调用 */
void settings_init(void);

void settings_open_wifi(void);
void settings_open_time(void);
void settings_open_location(void);

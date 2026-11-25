#pragma once

#include "lvgl.h"

/* 创建设置面板（隐藏状态），需在 LVGL 初始化后调用 */
void alarm_ui_init(void);

/* 打开设置面板 */
void alarm_ui_open(int hour, int minute, bool enabled);

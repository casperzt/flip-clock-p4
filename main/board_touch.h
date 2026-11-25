#pragma once

#include "lvgl.h"
#include "esp_err.h"

esp_err_t touch_init(lv_display_t *disp);

/* 绕过 esp_lcd_touch，直接读 GT911 原始状态/坐标（诊断用） */
esp_err_t gt911_raw_read(uint8_t *status, uint16_t *x, uint16_t *y);

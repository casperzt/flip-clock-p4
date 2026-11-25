#pragma once

#include "lvgl.h"
#include "esp_err.h"

esp_err_t display_init(lv_display_t **out_disp);
esp_err_t display_set_brightness(int percent);

/* 供截屏/推流使用 */
void *board_display_panel(void);

/* 面板重新同步：关显示→开显示，用来修复整屏重绘后的绿屏/失步 */
void board_display_resync(void);

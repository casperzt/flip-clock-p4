#pragma once

#include "lvgl.h"

void ui_clock_init(lv_display_t *disp);

/* 时间刚被设置时调用，立即刷新一次 */
void ui_clock_refresh_now(void);

/* 打开 INFO 页（供指针表页调用） */
void ui_clock_show_info(void);

/* 关闭 INFO 页 */
void ui_clock_hide_info(void);

/* 强制整屏重绘（关闭面板/信息页后修复可能的残影/绿屏） */
void ui_clock_redraw(void);

/* 闹钟 */
void ui_clock_set_alarm(int hour, int minute, bool enabled);
void ui_clock_show_ringing(bool on, int hour, int minute);

/* 天气更新后调用，刷新底部 */
void ui_clock_refresh_weather(void);

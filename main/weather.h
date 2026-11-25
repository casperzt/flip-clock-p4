#pragma once

#include <stdbool.h>

/* 启动后台任务：联网后每 20 分钟拉一次天气 */
void weather_start(void);

/* 设置坐标（默认深圳），保存到 NVS */
void weather_set_location(double lat, double lon);

/* 当前天气文本，例如 "26° 晴"；无数据返回空串 */
const char *weather_text(void);

/* IP 定位得到的城市名（ASCII），没有则空串 */
const char *weather_city(void);

/* 重新按 IP 定位（手动设过坐标后会重新启用自动定位） */
void weather_use_ip_location(void);

/* 位置模式 */
bool weather_auto_location(void);
void weather_set_auto_location(bool on);
/* 请求尽快刷新天气 */
void weather_refresh_now(void);

double weather_latitude(void);
double weather_longitude(void);

/* 手动选择城市时保存显示名 */
void weather_set_location_name(const char *name);

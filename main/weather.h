#pragma once

#include <stdbool.h>

/* 启动后台任务：联网后每 20 分钟拉一次天气 */
void weather_start(void);

/* 设置坐标（默认上海），保存到 NVS */
void weather_set_location(double lat, double lon);

/* 当前天气文本，例如 "26°C 晴"；无数据返回空串 */
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

/* ---------- 天气页用的扩展数据 ---------- */

typedef struct {
    int hour;    /* 0-23，当地小时 */
    int temp;    /* °C */
    int code;    /* WMO 天气代码 */
    bool is_day; /* 白天/夜间 */
} weather_hour_t;

/* 是否已经拿到过数据 */
bool weather_has_data(void);

/* 当前温度 / 天气代码 */
int weather_temp_now(void);
int weather_code_now(void);

/* 当前天气中文描述，如 "多云"；无数据返回空串 */
const char *weather_condition(void);

/* 今日最高/最低温度（°C） */
bool weather_high_low(int *hi, int *lo);

/* 日出时间 "06:30"；无数据返回空串 */
const char *weather_sunrise(void);

/* 从下一个整点开始的逐小时预报，返回条数 */
int weather_hourly_next(weather_hour_t *out, int max);

#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    int year;
    int month;   /* 1-12 */
    int day;     /* 1-30 */
    bool leap;   /* 闰月 */
} lunar_date_t;

/* 公历 -> 农历，支持 1900-2100 */
bool lunar_from_solar(int sy, int sm, int sd, lunar_date_t *out);

/* 输出形如 "十月十六" / "闰四月廿三" */
void lunar_format(const lunar_date_t *l, char *buf, size_t size);

/* 星期几，0=日 */
const char *weekday_cn(int wday);

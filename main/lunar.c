#include "lunar.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 1900-2100 农历数据表（每年一个 32 位值）：
 * bit 20-23: 闰月月份（0 表示无闰月）
 * bit 19:     闰月是否 30 天
 * bit 12-15:  12 月是否 30 天（从 1 月起，bit12 = 12 月）
 * bit 0-11:   每月是否 30 天（bit0 = 1 月）
 */
static const uint32_t s_lunar_info[] = {
    0x04bd8, 0x04ae0, 0x0a570, 0x054d5, 0x0d260, 0x0d950, 0x16554, 0x056a0, 0x09ad0, 0x055d2,
    0x04ae0, 0x0a5b6, 0x0a4d0, 0x0d250, 0x1d255, 0x0b540, 0x0d6a0, 0x0ada2, 0x095b0, 0x14977,
    0x04970, 0x0a4b0, 0x0b4b5, 0x06a50, 0x06d40, 0x1ab54, 0x02b60, 0x09570, 0x052f2, 0x04970,
    0x06566, 0x0d4a0, 0x0ea50, 0x06e95, 0x05ad0, 0x02b60, 0x186e3, 0x092e0, 0x1c8d7, 0x0c950,
    0x0d4a0, 0x1d8a6, 0x0b550, 0x056a0, 0x1a5b4, 0x025d0, 0x092d0, 0x0d2b2, 0x0a950, 0x0b557,
    0x06ca0, 0x0b550, 0x15355, 0x04da0, 0x0a5b0, 0x14573, 0x052b0, 0x0a9a8, 0x0e950, 0x06aa0,
    0x0aea6, 0x0ab50, 0x04b60, 0x0aae4, 0x0a570, 0x05260, 0x0f263, 0x0d950, 0x05b57, 0x056a0,
    0x096d0, 0x04dd5, 0x04ad0, 0x0a4d0, 0x0d4d4, 0x0d250, 0x0d558, 0x0b540, 0x0b6a0, 0x195a6,
    0x095b0, 0x049b0, 0x0a974, 0x0a4b0, 0x0b27a, 0x06a50, 0x06d40, 0x0af46, 0x0ab60, 0x09570,
    0x04af5, 0x04970, 0x064b0, 0x074a3, 0x0ea50, 0x06b58, 0x055c0, 0x0ab60, 0x096d5, 0x092e0,
    0x0c960, 0x0d954, 0x0d4a0, 0x0da50, 0x07552, 0x056a0, 0x0abb7, 0x025d0, 0x092d0, 0x0cab5,
    0x0a950, 0x0b4a0, 0x0baa4, 0x0ad50, 0x055d9, 0x04ba0, 0x0a5b0, 0x15176, 0x052b0, 0x0a930,
    0x07954, 0x06aa0, 0x0ad50, 0x05b52, 0x04b60, 0x0a6e6, 0x0a4e0, 0x0d260, 0x0ea65, 0x0d530,
    0x05aa0, 0x076a3, 0x096d0, 0x04afb, 0x04ad0, 0x0a4d0, 0x1d0b6, 0x0d250, 0x0d520, 0x0dd45,
    0x0b5a0, 0x056d0, 0x055b2, 0x049b0, 0x0a577, 0x0a4b0, 0x0aa50, 0x1b255, 0x06d20, 0x0ada0,
    0x14b63, 0x09370, 0x049f8, 0x04970, 0x064b0, 0x168a6, 0x0ea50, 0x06b20, 0x1a6c4, 0x0aae0,
    0x0a2e0, 0x0d2e3, 0x0c960, 0x0d557, 0x0d4a0, 0x0da50, 0x05d55, 0x056a0, 0x0a6d0, 0x055d4,
    0x052d0, 0x0a9b8, 0x0a950, 0x0b4a0, 0x0b6a6, 0x0ad50, 0x055a0, 0x0aba4, 0x0a5b0, 0x052b0,
    0x0b273, 0x06930, 0x07337, 0x06aa0, 0x0ad50, 0x14b55, 0x04b60, 0x0a570, 0x054e4, 0x0d160,
    0x0e968, 0x0d520, 0x0daa0, 0x16aa6, 0x056d0, 0x04ae0, 0x0a9d4, 0x0a2d0, 0x0d150, 0x0f252,
    0x0d520,
};

#define LUNAR_BASE_YEAR 1900

static int leap_month(int year)
{
    return (int)(s_lunar_info[year - LUNAR_BASE_YEAR] & 0xf);
}

static int leap_days(int year)
{
    if (leap_month(year) == 0) {
        return 0;
    }
    return (s_lunar_info[year - LUNAR_BASE_YEAR] & 0x10000) ? 30 : 29;
}

static int month_days(int year, int month)
{
    return (s_lunar_info[year - LUNAR_BASE_YEAR] & (0x10000 >> month)) ? 30 : 29;
}

static int year_days(int year)
{
    int sum = 348;
    for (int i = 0x8000; i > 0x8; i >>= 1) {
        sum += (s_lunar_info[year - LUNAR_BASE_YEAR] & i) ? 1 : 0;
    }
    return sum + leap_days(year);
}

/* 公历 1900-01-31 是农历 1900 年正月初一 */
static int solar_days_since_base(int y, int m, int d)
{
    static const int mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int days = 0;
    for (int yy = 1900; yy < y; yy++) {
        days += 365 + ((yy % 4 == 0 && yy % 100 != 0) || yy % 400 == 0);
    }
    for (int mm = 1; mm < m; mm++) {
        days += mdays[mm - 1];
        if (mm == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) {
            days += 1;
        }
    }
    days += d - 1;
    /* 1900-01-31 距 1900-01-01 是 30 天 */
    return days - 30;
}

bool lunar_from_solar(int sy, int sm, int sd, lunar_date_t *out)
{
    if (!out || sy < 1900 || sy > 2100 || sm < 1 || sm > 12 || sd < 1 || sd > 31) {
        return false;
    }
    int offset = solar_days_since_base(sy, sm, sd);
    if (offset < 0) {
        return false;
    }

    int year = LUNAR_BASE_YEAR;
    while (year < 2100) {
        int yd = year_days(year);
        if (offset < yd) {
            break;
        }
        offset -= yd;
        year++;
    }

    /* 当年月份的先后顺序（含闰月） */
    typedef struct {
        int m;
        bool leap;
        int days;
    } lm_t;
    lm_t list[13];
    int n = 0;
    const int leap = leap_month(year);
    for (int m = 1; m <= 12; m++) {
        list[n].m = m;
        list[n].leap = false;
        list[n].days = month_days(year, m);
        n++;
        if (leap == m) {
            list[n].m = m;
            list[n].leap = true;
            list[n].days = leap_days(year);
            n++;
        }
    }

    int idx = 0;
    while (idx < n && offset >= list[idx].days) {
        offset -= list[idx].days;
        idx++;
    }
    if (idx >= n) {
        return false;
    }

    out->year = year;
    out->month = list[idx].m;
    out->leap = list[idx].leap;
    out->day = offset + 1;
    return true;
}

static const char *s_month_names[] = {
    "", "正", "二", "三", "四", "五", "六", "七", "八", "九", "十", "冬", "腊",
};

static const char *s_digits[] = { "", "一", "二", "三", "四", "五", "六", "七", "八", "九" };

void lunar_format(const lunar_date_t *l, char *buf, size_t size)
{
    char day[16];
    if (l->day == 10) {
        snprintf(day, sizeof(day), "初十");
    } else if (l->day < 10) {
        snprintf(day, sizeof(day), "初%s", s_digits[l->day]);
    } else if (l->day < 20) {
        snprintf(day, sizeof(day), "十%s", s_digits[l->day % 10]);
    } else if (l->day == 20) {
        snprintf(day, sizeof(day), "二十");
    } else if (l->day < 30) {
        snprintf(day, sizeof(day), "廿%s", s_digits[l->day % 10]);
    } else {
        snprintf(day, sizeof(day), "三十");
    }

    snprintf(buf, size, "%s%s月%s", l->leap ? "闰" : "", s_month_names[l->month], day);
}

const char *weekday_cn(int wday)
{
    static const char *names[] = { "周日", "周一", "周二", "周三", "周四", "周五", "周六" };
    if (wday < 0 || wday > 6) {
        return "";
    }
    return names[wday];
}

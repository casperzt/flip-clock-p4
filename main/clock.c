#include "clock.h"

#include <stdlib.h>
#include <sys/time.h>

#include "esp_log.h"

static const char *TAG = "clock";
static bool s_set;

void clock_init(void)
{
    setenv("TZ", "CST-8", 1);
    tzset();
}

bool clock_is_set(void)
{
    return s_set;
}

void clock_set_epoch(uint32_t epoch)
{
    struct timeval tv = {
        .tv_sec = (time_t)epoch,
        .tv_usec = 0,
    };
    settimeofday(&tv, NULL);
    s_set = true;

    struct tm tm;
    localtime_r(&tv.tv_sec, &tm);
    ESP_LOGI(TAG, "time set: %04d-%02d-%02d %02d:%02d:%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
}

int clock_get(struct tm *out)
{
    if (!s_set) {
        return -1;
    }
    time_t now = time(NULL);
    localtime_r(&now, out);
    return 0;
}

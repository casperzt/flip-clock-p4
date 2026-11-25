#include "clock.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NVS_NS    "clk"
#define NVS_EPOCH "epoch"

static const char *TAG = "clock";
static bool s_set;

static void save_epoch(void)
{
    if (!s_set) {
        return;
    }
    time_t now = time(NULL);
    if (now < 1700000000) {
        return;
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, NVS_EPOCH, (uint32_t)now);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void persist_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(300 * 1000));   /* 每 5 分钟落盘一次 */
        save_epoch();
    }
}

void clock_init(void)
{
    setenv("TZ", "CST-8", 1);
    tzset();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }

    if (err == ESP_OK) {
        nvs_handle_t h;
        if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
            uint32_t e = 0;
            if (nvs_get_u32(h, NVS_EPOCH, &e) == ESP_OK && e > 1700000000UL) {
                clock_set_epoch(e);
                time_t t = (time_t)e;
                struct tm tm;
                localtime_r(&t, &tm);
                ESP_LOGI(TAG, "restored last time: %04d-%02d-%02d %02d:%02d:%02d",
                         tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                         tm.tm_hour, tm.tm_min, tm.tm_sec);
            }
            nvs_close(h);
        }
    }

    xTaskCreate(persist_task, "clock_save", 3072, NULL, 2, NULL);
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

    save_epoch();
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

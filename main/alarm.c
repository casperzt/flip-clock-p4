#include "alarm.h"

#include <math.h>
#include <stdio.h>

#include "board_audio.h"
#include "clock.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ui_clock.h"

#define NVS_NS      "clk"
#define NVS_AL_H    "al_h"
#define NVS_AL_M    "al_m"
#define NVS_AL_ON   "al_on"

#define FRAMES 4410 /* 100ms @44.1k */

static const char *TAG = "alarm";

static int s_hour = -1;
static int s_minute = -1;
static bool s_enabled;
static volatile bool s_ringing;
static int s_fired_yday = -1;
static bool s_audio_ready;

static int16_t s_buf[FRAMES * 2];
static double s_phase;

static void save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_i32(h, NVS_AL_H, s_hour);
    nvs_set_i32(h, NVS_AL_M, s_minute);
    nvs_set_u8(h, NVS_AL_ON, s_enabled ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

static void gen(int frames, bool on)
{
    const double step = 2.0 * M_PI * 1000.0 / 44100.0;
    for (int i = 0; i < frames; i++) {
        int16_t v = 0;
        if (on) {
            v = (int16_t)(sin(s_phase) * 9000);
            s_phase += step;
            if (s_phase > 2.0 * M_PI) {
                s_phase -= 2.0 * M_PI;
            }
        }
        s_buf[2 * i] = v;
        s_buf[2 * i + 1] = v;
    }
    audio_write(s_buf, (size_t)frames * 2 * sizeof(int16_t));
}

static void ring(void)
{
    if (!s_audio_ready) {
        if (audio_init() != ESP_OK || audio_open(44100, 2, 16) != ESP_OK) {
            ESP_LOGE(TAG, "audio init failed, silent alarm");
            return;
        }
        s_audio_ready = true;
    }
    audio_set_volume(65);

    while (s_ringing) {
        for (int i = 0; i < 3 && s_ringing; i++) {
            gen(FRAMES, true);
            gen(FRAMES, false);
        }
        for (int i = 0; i < 4 && s_ringing; i++) {
            gen(FRAMES, false);
        }
    }

    gen(FRAMES, false);
    audio_set_volume(0);
}

static void alarm_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(200));

        if (!s_enabled || s_hour < 0) {
            continue;
        }
        struct tm tm;
        if (clock_get(&tm) != 0) {
            continue;
        }
        if (tm.tm_hour == s_hour && tm.tm_min == s_minute && s_fired_yday != tm.tm_yday) {
            s_fired_yday = tm.tm_yday;
            s_ringing = true;
            ESP_LOGI(TAG, "ALARM %02d:%02d", s_hour, s_minute);
            ui_clock_show_ringing(true, s_hour, s_minute);
            ring();
            ui_clock_show_ringing(false, 0, 0);
            ESP_LOGI(TAG, "alarm stopped");
        }
    }
}

void alarm_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        int32_t v = 0;
        if (nvs_get_i32(h, NVS_AL_H, &v) == ESP_OK) {
            s_hour = (int)v;
        }
        if (nvs_get_i32(h, NVS_AL_M, &v) == ESP_OK) {
            s_minute = (int)v;
        }
        uint8_t on = 0;
        if (nvs_get_u8(h, NVS_AL_ON, &on) == ESP_OK) {
            s_enabled = on != 0;
        }
        nvs_close(h);
    }

    if (s_hour < 0) {
        s_enabled = false;
    }
    ESP_LOGI(TAG, "alarm %s %02d:%02d", s_enabled ? "on" : "off", s_hour, s_minute);
    ui_clock_set_alarm(s_hour, s_minute, s_enabled);

    xTaskCreate(alarm_task, "alarm", 6144, NULL, 4, NULL);
}

bool alarm_set(int hour, int minute)
{
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return false;
    }
    s_hour = hour;
    s_minute = minute;
    s_enabled = true;
    s_fired_yday = -1;
    save();
    ui_clock_set_alarm(s_hour, s_minute, true);
    ESP_LOGI(TAG, "alarm set to %02d:%02d", hour, minute);
    return true;
}

void alarm_clear(void)
{
    s_enabled = false;
    save();
    ui_clock_set_alarm(s_hour, s_minute, false);
    ESP_LOGI(TAG, "alarm cleared");
}

bool alarm_get(int *hour, int *minute)
{
    if (hour) {
        *hour = s_hour;
    }
    if (minute) {
        *minute = s_minute;
    }
    return s_enabled && s_hour >= 0;
}

bool alarm_is_ringing(void)
{
    return s_ringing;
}

void alarm_stop(void)
{
    s_ringing = false;
}

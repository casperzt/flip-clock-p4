#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_display.h"
#include "board_i2c.h"
#include "board_touch.h"
#include "alarm.h"
#include "clock.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui_alarm.h"
#include "ui_analog.h"
#include "ui_calendar.h"
#include "ui_weather.h"
#include "ui_calc.h"
#include "ui_clock.h"
#include "ui_settings.h"
#include "screen_stream.h"
#include "weather.h"
#include "wifi_time.h"

static const char *TAG = "main";

static void stream_boot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(5000));
    screen_stream_start();
    vTaskDelete(NULL);
}

static int s_brightness = 80;

static void handle_cmd(char *line)
{
    if (strncmp(line, "TAP ", 4) == 0) {
        int x = -1, y = -1;
        if (sscanf(line + 4, "%d %d", &x, &y) == 2) {
            screen_tap(x, y);
        }
        return;
    }
    if (strcmp(line, "SYNC") == 0) {
        weather_refresh_now();
        wifi_time_resync();
        ui_clock_refresh_now();
        ESP_LOGI(TAG, "sync requested");
        return;
    }
    if (strncmp(line, "PAGE ", 5) == 0) {
        int p = atoi(line + 5);
        ui_clock_hide_info();
        ui_analog_show(false);
        ui_calendar_show(false);
        ui_weather_show(false);
        ui_calc_show(false);
        if (p == 1) {
            ui_analog_show(true);
        } else if (p == 2) {
            ui_calendar_show(true);
        } else if (p == 3) {
            ui_weather_show(true);
        } else if (p == 4) {
            ui_clock_show_info();
        } else if (p == 5) {
            ui_calc_show(true);
        }
        return;
    }
    if (line[0] == 'M' && (line[1] == '0' || line[1] == '1') && line[2] == '\0') {
        screen_mirror_set(line[1] == '1');
        return;
    }

    switch (line[0]) {
    case 'T':
    case 't':
        if (line[1] != '\0') {
            unsigned long epoch = strtoul(line + 1, NULL, 10);
            if (epoch > 1000000000UL) {
                clock_set_epoch((uint32_t)epoch);
                ui_clock_refresh_now();
            } else {
                ESP_LOGW(TAG, "bad epoch: %s", line + 1);
            }
        } else {
            struct tm tm;
            if (clock_get(&tm) == 0) {
                ESP_LOGI(TAG, "now: %04d-%02d-%02d %02d:%02d:%02d",
                         tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                         tm.tm_hour, tm.tm_min, tm.tm_sec);
            } else {
                ESP_LOGI(TAG, "time not set");
            }
        }
        break;
    case 'b': {
        int v = atoi(line + 1);
        if (v < 0) {
            v = 0;
        }
        if (v > 100) {
            v = 100;
        }
        s_brightness = v;
        display_set_brightness(v);
        ESP_LOGI(TAG, "brightness %d%%", v);
        break;
    }
    case 'W': {
        char *comma = strchr(line + 1, ',');
        if (comma) {
            *comma = '\0';
            if (wifi_time_set_credentials(line + 1, comma + 1) == 0) {
                ESP_LOGI(TAG, "wifi configured, connecting...");
            } else {
                ESP_LOGE(TAG, "wifi config save failed");
            }
        } else {
            ESP_LOGW(TAG, "usage: W<ssid>,<password>");
        }
        break;
    }
    case 'A': {
        if (line[1] == '\0' || line[1] == '?') {
            int hh, mm;
            if (alarm_get(&hh, &mm)) {
                ESP_LOGI(TAG, "alarm on %02d:%02d", hh, mm);
            } else {
                ESP_LOGI(TAG, "alarm off");
            }
        } else if (line[1] == '0' && line[2] == '\0') {
            alarm_clear();
        } else {
            int hh = 0, mm = 0;
            if (sscanf(line + 1, "%d:%d", &hh, &mm) == 2) {
                if (!alarm_set(hh, mm)) {
                    ESP_LOGW(TAG, "bad alarm time");
                }
            } else {
                ESP_LOGW(TAG, "usage: A<HH:MM> | A0 | A?");
            }
        }
        break;
    }
    case 'x':
        alarm_stop();
        break;
    case 'R':
        board_display_resync();
        ui_clock_redraw();
        break;
    case 'M':
        screen_mirror_set(line[1] == '1');
        break;
    case 'S':
        ESP_LOGI(TAG, "toggling screen stream...");
        screen_stream_set_enabled(!screen_stream_enabled());
        ESP_LOGI(TAG, "stream %s", screen_stream_enabled() ? "ON" : "OFF");
        break;
    case 'L':
        ESP_LOGI(TAG, "location %.4f, %.4f mode=%s city=%s", weather_latitude(), weather_longitude(),
                 weather_auto_location() ? "AUTO" : "MANUAL", weather_city());
        break;
    case 'G':
        weather_use_ip_location();
        ESP_LOGI(TAG, "will re-locate by ip on next fetch");
        break;
    case 'P': {
        double lat = 0, lon = 0;
        if (sscanf(line + 1, "%lf,%lf", &lat, &lon) == 2) {
            weather_set_location(lat, lon);
            ESP_LOGI(TAG, "weather location set");
        } else {
            ESP_LOGW(TAG, "usage: P<lat>,<lon>");
        }
        break;
    }
    case 'w':
        ESP_LOGI(TAG, "wifi: %s, ip %s, sntp %s", wifi_time_connected() ? "connected" : "not connected",
                 wifi_time_ip(), wifi_time_synced() ? "synced" : "not synced");
        break;
    case '?':
        ESP_LOGI(TAG, "commands: T<epoch>=set time  t=print time  b<0-100>=brightness");
        ESP_LOGI(TAG, "          S=start screen stream  L=location  A<HH:MM>=alarm");
        ESP_LOGI(TAG, "          W<ssid>,<password>=set wifi  w=wifi status");
        break;
    default:
        break;
    }
}

static void console_task(void *arg)
{
    (void)arg;
    char line[64];
    int n = 0;
    setvbuf(stdin, NULL, _IONBF, 0);

    for (;;) {
        int c = fgetc(stdin);
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (c == '\r' || c == '\n') {
            line[n] = '\0';
            if (n > 0) {
                handle_cmd(line);
            }
            n = 0;
        } else if (n < (int)sizeof(line) - 1) {
            line[n++] = (char)c;
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "flip clock boot");

    ESP_ERROR_CHECK(board_i2c_init());
    clock_init();

    lv_display_t *disp = NULL;
    ESP_ERROR_CHECK(display_init(&disp));
    ESP_ERROR_CHECK(touch_init(disp));

    ui_clock_init(disp);
    ui_analog_init();
    ui_calendar_init();
    ui_weather_init();
    ui_calc_init();
    alarm_ui_init();
    settings_init();
    display_set_brightness(s_brightness);

    /* screen_stream_start();  // 调试中，先关闭 */
    alarm_init();
    wifi_time_start();
    weather_start();

    xTaskCreate(console_task, "console", 4096, NULL, 3, NULL);

    /* 开机 5 秒后启动 HTTP 服务（默认不推流，等客户端 /stream?on=1 再开启编码） */
    xTaskCreate(stream_boot_task, "streamboot", 4096, NULL, 2, NULL);

    ESP_LOGI(TAG, "ready: T<epoch> to set time, t to print, b<0-100> brightness");
}

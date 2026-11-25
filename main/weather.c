#include "weather.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "ui_clock.h"
#include "wifi_time.h"
#include "clock.h"

#define NVS_NS  "clk"
#define NVS_LAT "lat"
#define NVS_LON "lon"
#define NVS_GEO "geo"
#define NVS_CITY "wcity"

static const char *TAG = "weather";

static char s_text[40] = "";
static char s_city[40] = "";
static double s_lat = 31.2304;   /* 上海人民广场 */
static double s_lon = 121.4737;
static bool s_auto_geo = false;
static volatile bool s_geo_done;
static volatile bool s_refresh_req;

static int s_now_temp;
static int s_now_code;
static bool s_has;
static int s_hi;
static int s_lo;
static bool s_has_hl;
static char s_sunrise[8] = "";
static weather_hour_t s_hours[48];
static int s_hour_count;

static const char *wmo_cn(int code)
{
    if (code == 0) {
        return "晴";
    }
    if (code <= 2) {
        return "多云";
    }
    if (code == 3) {
        return "阴";
    }
    if (code >= 45 && code <= 48) {
        return "雾";
    }
    if (code >= 51 && code <= 57) {
        return "毛毛雨";
    }
    if (code >= 61 && code <= 67) {
        return "雨";
    }
    if (code >= 71 && code <= 77) {
        return "雪";
    }
    if (code >= 80 && code <= 82) {
        return "阵雨";
    }
    if (code >= 85 && code <= 86) {
        return "阵雪";
    }
    if (code >= 95) {
        return "雷雨";
    }
    return "";
}

static bool http_get(const char *url, char *buf, size_t len)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return false;
    }
    bool ok = false;
    if (esp_http_client_open(client, 0) == ESP_OK) {
        esp_http_client_fetch_headers(client);
        int r = esp_http_client_read(client, buf, (int)len - 1);
        if (r > 0) {
            buf[r] = '\0';
            ok = true;
        }
        esp_http_client_close(client);
    }
    esp_http_client_cleanup(client);
    return ok;
}

static void save_location(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, NVS_LAT, &s_lat, sizeof(s_lat));
        nvs_set_blob(h, NVS_LON, &s_lon, sizeof(s_lon));
        uint8_t g = s_auto_geo ? 1 : 0;
        nvs_set_u8(h, NVS_GEO, g);
        nvs_set_str(h, NVS_CITY, s_city);
        nvs_commit(h);
        nvs_close(h);
    }
}

static bool geo_locate(void)
{
    char *buf = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
    if (!buf) {
        return false;
    }
    bool ok = false;
    if (http_get("http://ip-api.com/json/?fields=status,lat,lon,city", buf, 1024) &&
        strstr(buf, "\"status\":\"success\"")) {
        const char *plat = strstr(buf, "\"lat\":");
        const char *plon = strstr(buf, "\"lon\":");
        const char *pcity = strstr(buf, "\"city\":");
        if (plat && plon) {
            s_lat = strtod(plat + 6, NULL);
            s_lon = strtod(plon + 6, NULL);
            s_city[0] = '\0';
            if (pcity) {
                pcity = strchr(pcity + 7, '"');
                if (pcity) {
                    pcity++;
                    size_t i = 0;
                    while (*pcity && *pcity != '"' && i < sizeof(s_city) - 1) {
                        s_city[i++] = *pcity++;
                    }
                    s_city[i] = '\0';
                }
            }
            save_location();
            ok = true;
            ESP_LOGI(TAG, "ip location: %.4f, %.4f %s", s_lat, s_lon, s_city);
        }
    }
    heap_caps_free(buf);
    return ok;
}

static int parse_int_list(const char *obj, const char *key, int *out, int max)
{
    const char *p = strstr(obj, key);
    if (!p) {
        return 0;
    }
    p = strchr(p, '[');
    if (!p) {
        return 0;
    }
    p++;
    int n = 0;
    while (n < max && *p && *p != ']') {
        while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r') {
            p++;
        }
        if (*p == ']' || !*p) {
            break;
        }
        char *endp = NULL;
        double v = strtod(p, &endp);
        if (!endp || endp == p) {
            break;
        }
        out[n++] = (int)lround(v);
        p = endp;
    }
    return n;
}

static int parse_hour_list(const char *obj, int *out, int max)
{
    const char *p = strstr(obj, "\"time\":");
    if (!p) {
        return 0;
    }
    p = strchr(p, '[');
    if (!p) {
        return 0;
    }
    p++;
    int n = 0;
    while (n < max && *p) {
        const char *q = strchr(p, '"');
        if (!q) {
            break;
        }
        const char *t = strchr(q + 1, 'T');
        if (!t) {
            break;
        }
        out[n++] = atoi(t + 1);
        const char *e = strchr(t, '"');
        if (!e) {
            break;
        }
        p = e + 1;
        if (*p == ']') {
            break;
        }
    }
    return n;
}

static bool fetch_once(void)
{
    char url[320];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,relative_humidity_2m,weather_code"
             "&hourly=temperature_2m,weather_code,is_day"
             "&daily=temperature_2m_max,temperature_2m_min,sunrise"
             "&timezone=auto&forecast_days=2",
             s_lat, s_lon);

    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 20000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return false;
    }
    esp_http_client_set_header(client, "User-Agent", "flip-clock/1.0");

    bool ok = false;
    const int cap = 16384;
    char *buf = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!buf) {
        esp_http_client_cleanup(client);
        return false;
    }

    if (esp_http_client_open(client, 0) == ESP_OK) {
        esp_http_client_fetch_headers(client);
        int total = 0;
        while (total < cap - 1) {
            int r = esp_http_client_read(client, buf + total, cap - 1 - total);
            if (r <= 0) {
                break;
            }
            total += r;
        }
        buf[total] = '\0';
        if (total > 0) {
            const char *curj = strstr(buf, "\"current\":");
            const char *pt = curj ? strstr(curj, "\"temperature_2m\"") : NULL;
            const char *pc = curj ? strstr(curj, "\"weather_code\"") : NULL;
            if (pt && pc) {
                pt = strchr(pt, ':');
                pc = strchr(pc, ':');
                if (pt && pc) {
                    double temp = strtod(pt + 1, NULL);
                    int code = (int)strtod(pc + 1, NULL);
                    s_now_temp = (int)lround(temp);
                    s_now_code = code;
                    s_has = true;
                    snprintf(s_text, sizeof(s_text), "%.0f°C %s", temp, wmo_cn(code));
                }
            }

            /* 今日最高/最低 + 日出 */
            const char *dj = strstr(buf, "\"daily\":");
            if (dj) {
                int hi[1] = { 0 }, lo[1] = { 0 };
                if (parse_int_list(dj, "\"temperature_2m_max\"", hi, 1) == 1 &&
                    parse_int_list(dj, "\"temperature_2m_min\"", lo, 1) == 1) {
                    s_hi = hi[0];
                    s_lo = lo[0];
                    s_has_hl = true;
                }
                const char *ps = strstr(dj, "\"sunrise\":");
                const char *q = ps ? strchr(ps, '"') : NULL;
                const char *t = q ? strchr(q + 1, 'T') : NULL;
                if (t) {
                    snprintf(s_sunrise, sizeof(s_sunrise), "%.5s", t + 1);
                }
            }

            /* 逐小时预报 */
            const char *hj = strstr(buf, "\"hourly\":");
            if (hj) {
                int hh[48], ht[48], hc[48], hd[48];
                int nt = parse_hour_list(hj, hh, 48);
                int ntemp = parse_int_list(hj, "\"temperature_2m\"", ht, 48);
                int ncode = parse_int_list(hj, "\"weather_code\"", hc, 48);
                int nday = parse_int_list(hj, "\"is_day\"", hd, 48);
                int n = nt;
                if (ntemp < n) {
                    n = ntemp;
                }
                if (ncode < n) {
                    n = ncode;
                }
                s_hour_count = 0;
                for (int i = 0; i < n; i++) {
                    s_hours[s_hour_count].hour = hh[i];
                    s_hours[s_hour_count].temp = ht[i];
                    s_hours[s_hour_count].code = hc[i];
                    s_hours[s_hour_count].is_day = (i < nday) ? (hd[i] != 0) : true;
                    s_hour_count++;
                }
            }

            ok = s_has;
            ESP_LOGI(TAG, "weather: %s hi=%d lo=%d sunrise=%s hours=%d (%d bytes)",
                     s_text, s_hi, s_lo, s_sunrise, s_hour_count, total);
        } else {
            ESP_LOGW(TAG, "read failed: %d", total);
        }
        esp_http_client_close(client);
    } else {
        ESP_LOGW(TAG, "connect failed");
    }

    esp_http_client_cleanup(client);
    heap_caps_free(buf);
    return ok;
}

static void weather_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (!wifi_time_connected()) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        if (s_auto_geo && !s_geo_done) {
            if (!geo_locate()) {
                ESP_LOGW(TAG, "ip locate failed, retrying");
                vTaskDelay(pdMS_TO_TICKS(30 * 1000));
                continue;
            }
            s_geo_done = true;
        }

        if (fetch_once()) {
            ui_clock_refresh_weather();
            /* 每 20 分钟一次，但允许被 weather_refresh_now() 提前唤醒 */
            for (int i = 0; i < 1200 && !s_refresh_req; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        } else {
            for (int i = 0; i < 60 && !s_refresh_req; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
        s_refresh_req = false;
    }
}

void weather_start(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s_lat);
        nvs_get_blob(h, NVS_LAT, &s_lat, &len);
        len = sizeof(s_lon);
        nvs_get_blob(h, NVS_LON, &s_lon, &len);
        uint8_t g = 0;   /* 默认不按 IP 定位（出口 IP 可能不准） */
        if (nvs_get_u8(h, NVS_GEO, &g) == ESP_OK) {
            s_auto_geo = g != 0;
        }
        size_t cl = sizeof(s_city);
        if (nvs_get_str(h, NVS_CITY, s_city, &cl) != ESP_OK) {
            s_city[0] = '\0';
        }
        nvs_close(h);
    }
    ESP_LOGI(TAG, "location %.4f, %.4f", s_lat, s_lon);
    xTaskCreate(weather_task, "weather", 8192, NULL, 3, NULL);
}

void weather_set_location(double lat, double lon)
{
    s_lat = lat;
    s_lon = lon;
    s_auto_geo = false;
    save_location();   /* 同时把 geo=0 写进去，重启后不会再按 IP 定位 */
    ESP_LOGI(TAG, "location set %.4f, %.4f (auto ip location off)", lat, lon);
}

const char *weather_text(void)
{
    return s_text;
}

const char *weather_city(void)
{
    return s_city;
}

void weather_use_ip_location(void)
{
    s_auto_geo = true;
    save_location();
    ESP_LOGI(TAG, "auto ip location enabled");
}

bool weather_auto_location(void)
{
    return s_auto_geo;
}

void weather_set_auto_location(bool on)
{
    s_auto_geo = on;
    save_location();
    if (on) {
        s_geo_done = false;
    } else {
        s_geo_done = true;
    }
    s_refresh_req = true;
    ESP_LOGI(TAG, "location mode: %s", on ? "AUTO (ip)" : "MANUAL");
}

void weather_refresh_now(void)
{
    s_refresh_req = true;
}

double weather_latitude(void)
{
    return s_lat;
}

double weather_longitude(void)
{
    return s_lon;
}

void weather_set_location_name(const char *name)
{
    snprintf(s_city, sizeof(s_city), "%s", name ? name : "");
    save_location();
}

bool weather_has_data(void)
{
    return s_has;
}

int weather_temp_now(void)
{
    return s_now_temp;
}

int weather_code_now(void)
{
    return s_now_code;
}

const char *weather_condition(void)
{
    return s_has ? wmo_cn(s_now_code) : "";
}

bool weather_high_low(int *hi, int *lo)
{
    if (!s_has_hl) {
        return false;
    }
    if (hi) {
        *hi = s_hi;
    }
    if (lo) {
        *lo = s_lo;
    }
    return true;
}

const char *weather_sunrise(void)
{
    return s_sunrise;
}

int weather_hourly_next(weather_hour_t *out, int max)
{
    struct tm tm;
    if (!out || max <= 0 || s_hour_count == 0 || clock_get(&tm) != 0) {
        return 0;
    }

    int start = -1;
    for (int i = 0; i + 1 < s_hour_count; i++) {
        if (s_hours[i].hour <= tm.tm_hour && s_hours[i + 1].hour > tm.tm_hour) {
            start = i + 1;
            break;
        }
    }
    if (start < 0) {
        for (int i = 1; i < s_hour_count; i++) {
            if (s_hours[i].hour == 0) {
                start = i;
                break;
            }
        }
    }
    if (start < 0) {
        start = 0;
    }

    int n = 0;
    for (int i = start; i < s_hour_count && n < max; i++) {
        out[n++] = s_hours[i];
    }
    return n;
}

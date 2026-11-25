#include "wifi_time.h"

#include <string.h>
#include <time.h>

#include "clock.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ui_clock.h"

#define NVS_NS   "clk"
#define NVS_SSID "ssid"
#define NVS_PASS "pass"

#define WIFI_CONNECTED_BIT BIT0

static const char *TAG = "wifi";

static EventGroupHandle_t s_events;
static char s_ip[20] = "-";
static volatile bool s_connected;
static volatile bool s_synced;
static volatile bool s_started;

static bool load_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t sl = ssid_len, pl = pass_len;
    bool ok = (nvs_get_str(h, NVS_SSID, ssid, &sl) == ESP_OK);
    if (ok) {
        pass[0] = '\0';
        nvs_get_str(h, NVS_PASS, pass, &pl);
    }
    nvs_close(h);
    return ok;
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        xEventGroupClearBits(s_events, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "disconnected, retrying...");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        s_connected = true;
        xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "got ip: %s", s_ip);
    }
}

static void sntp_task(void *arg)
{
    (void)arg;
    xEventGroupWaitBits(s_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    if (!esp_sntp_enabled()) {
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "ntp.aliyun.com");
        esp_sntp_setservername(1, "cn.pool.ntp.org");
        esp_sntp_setservername(2, "pool.ntp.org");
        esp_sntp_init();
    }

    for (;;) {
        if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            time_t now = time(NULL);
            if (now > 1700000000) {
                s_synced = true;
                clock_set_epoch((uint32_t)now);
                ui_clock_refresh_now();
                ESP_LOGI(TAG, "sntp synced: %lld", (long long)now);
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelete(NULL);
}

static void wifi_task(void *arg)
{
    (void)arg;
    char ssid[33] = { 0 }, pass[65] = { 0 };

    if (!load_credentials(ssid, sizeof(ssid), pass, sizeof(pass))) {
        ESP_LOGW(TAG, "no wifi credentials (send W<ssid>,<pass>)");
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = esp_hosted_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_hosted_init: %s", esp_err_to_name(err));
    }
    err = esp_hosted_connect_to_slave();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "connect_to_slave: %s", esp_err_to_name(err));
    }

    ESP_ERROR_CHECK(esp_netif_init());
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop: %s", esp_err_to_name(err));
    }
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL, NULL));

    wifi_config_t cfg = { 0 };
    memcpy(cfg.sta.ssid, ssid, strnlen(ssid, sizeof(cfg.sta.ssid)));
    memcpy(cfg.sta.password, pass, strnlen(pass, sizeof(cfg.sta.password)));
    cfg.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "connecting to \"%s\"...", ssid);
    xTaskCreate(sntp_task, "sntp", 4096, NULL, 3, NULL);
    s_started = true;
    vTaskDelete(NULL);
}

void wifi_time_resync(void)
{
    if (esp_sntp_enabled()) {
        esp_sntp_restart();
        ESP_LOGI(TAG, "sntp resync requested");
    }
}

void wifi_time_start(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs init: %s", esp_err_to_name(err));
        return;
    }

    s_events = xEventGroupCreate();
    xTaskCreate(wifi_task, "wifi", 8192, NULL, 4, NULL);
}

int wifi_time_set_credentials(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return -1;
    }
    nvs_set_str(h, NVS_SSID, ssid);
    nvs_set_str(h, NVS_PASS, pass);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "credentials saved for \"%s\"", ssid);

    /* 已经跑起来就直接改配置重连 */
    if (s_started) {
        wifi_config_t cfg = { 0 };
        memcpy(cfg.sta.ssid, ssid, strnlen(ssid, sizeof(cfg.sta.ssid)));
        memcpy(cfg.sta.password, pass, strnlen(pass, sizeof(cfg.sta.password)));
        cfg.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        esp_wifi_disconnect();
        esp_wifi_set_config(WIFI_IF_STA, &cfg);
        esp_wifi_connect();
    } else {
        wifi_time_start();
    }
    return 0;
}

bool wifi_time_connected(void)
{
    return s_connected;
}

bool wifi_time_synced(void)
{
    return s_synced;
}

const char *wifi_time_ip(void)
{
    return s_ip;
}

int wifi_time_scan(wifi_ap_t *out, int max)
{
    if (!s_started || !out || max <= 0) {
        return 0;
    }
    static wifi_ap_record_t recs[24];
    wifi_scan_config_t sc = { 0 };

    for (int attempt = 0; attempt < 3; attempt++) {
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(300));

        if (esp_wifi_scan_start(&sc, true) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        uint16_t num = 0;
        if (esp_wifi_scan_get_ap_num(&num) != ESP_OK || num == 0) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        uint16_t n = num > 24 ? 24 : num;
        if (esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) {
            continue;
        }
        int k = 0;
        for (int i = 0; i < n && k < max; i++) {
            if (recs[i].ssid[0] == 0) {
                continue;
            }
            memset(&out[k], 0, sizeof(out[k]));
            snprintf(out[k].ssid, sizeof(out[k].ssid), "%s", (const char *)recs[i].ssid);
            out[k].rssi = recs[i].rssi;
            out[k].auth = recs[i].authmode != WIFI_AUTH_OPEN;
            k++;
        }
        ESP_LOGI(TAG, "scan found %d ssid(s)", k);
        return k;
    }
    ESP_LOGW(TAG, "scan failed");
    return 0;
}

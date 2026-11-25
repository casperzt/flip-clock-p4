#pragma once

#include <stdbool.h>
#include <stdint.h>

/* 启动时调用：读 NVS 里的 Wi-Fi 配置，有就后台连接并 SNTP 对时 */
void wifi_time_start(void);

/* 串口设置账号：保存到 NVS 并立即重连。返回 0 成功 */
int wifi_time_set_credentials(const char *ssid, const char *pass);

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool auth;
} wifi_ap_t;

/* 阻塞扫描附近 Wi-Fi，返回数量（最多 max 个） */
int wifi_time_scan(wifi_ap_t *out, int max);

bool wifi_time_connected(void);
bool wifi_time_synced(void);
const char *wifi_time_ip(void);

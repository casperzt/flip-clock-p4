#include "ui_settings.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "clock.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "ui_clock.h"
#include "weather.h"
#include "wifi_time.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "settings";

LV_FONT_DECLARE(font_cjk);
LV_FONT_DECLARE(font_cjk20);
LV_FONT_DECLARE(font_cjk36);

/* ---------------- 公共 ---------------- */
static lv_obj_t *make_backdrop(lv_event_cb_t close_cb)
{
    lv_obj_t *bd = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(bd, 0, 0);
    lv_obj_set_size(bd, 800, 480);
    lv_obj_set_style_bg_color(bd, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(bd, LV_OPA_70, 0);
    lv_obj_set_style_border_width(bd, 0, 0);
    lv_obj_set_style_pad_all(bd, 0, 0);
    lv_obj_clear_flag(bd, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bd, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bd, close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(bd, LV_OBJ_FLAG_HIDDEN);
    return bd;
}

static lv_obj_t *make_panel(lv_obj_t *bd, int w, int h, int y)
{
    lv_obj_t *p = lv_obj_create(bd);
    lv_obj_set_size(p, w, h);
    lv_obj_align(p, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_radius(p, 18, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(0x15181C), 0);
    lv_obj_set_style_border_color(p, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE);
    return p;
}

static lv_obj_t *make_btn(lv_obj_t *parent, int x, int y, int w, int h, const char *txt,
                          lv_event_cb_t cb, uint32_t color, const lv_font_t *font)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font ? font : &lv_font_montserrat_20, 0);
    lv_obj_center(l);
    return b;
}

/* ================= Wi-Fi ================= */
static lv_obj_t *s_wifi_bd;
static lv_obj_t *s_wifi_ssid;
static lv_obj_t *s_wifi_pass;
static lv_obj_t *s_wifi_kb;
static lv_obj_t *s_wifi_hint;
static lv_obj_t *s_wifi_list;
static wifi_ap_t s_aps[20];
static int s_ap_count;

static void wifi_close_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_wifi_bd, LV_OBJ_FLAG_HIDDEN);
}

static void wifi_field_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    lv_keyboard_set_textarea(s_wifi_kb, ta);
    lv_obj_clear_flag(s_wifi_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_wifi_kb);
}

static void wifi_kb_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(s_wifi_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void wifi_save_cb(lv_event_t *e)
{
    (void)e;
    const char *ssid = lv_textarea_get_text(s_wifi_ssid);
    const char *pass = lv_textarea_get_text(s_wifi_pass);
    if (!ssid[0]) {
        lv_label_set_text(s_wifi_hint, "SSID is empty");
        return;
    }
    int rc = wifi_time_set_credentials(ssid, pass);
    lv_label_set_text(s_wifi_hint, rc == 0 ? "saved, connecting..." : "save failed");
    lv_obj_add_flag(s_wifi_kb, LV_OBJ_FLAG_HIDDEN);
}

static void wifi_forget_cb(lv_event_t *e)
{
    (void)e;
    lv_textarea_set_text(s_wifi_pass, "");
    lv_label_set_text(s_wifi_hint, "clear password then SAVE");
}

static void ap_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < s_ap_count) {
        lv_textarea_set_text(s_wifi_ssid, s_aps[idx].ssid);
        lv_label_set_text(s_wifi_hint, s_aps[idx].auth ? "enter password" : "open network");
    }
}

static void list_msg(const char *txt)
{
    lv_obj_clean(s_wifi_list);
    lv_obj_t *l = lv_label_create(s_wifi_list);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0x8A939E), 0);
}

static void wifi_scan_task(void *arg)
{
    (void)arg;
    int n = wifi_time_scan(s_aps, 20);

    lvgl_port_lock(0);
    s_ap_count = n;
    lv_obj_clean(s_wifi_list);
    if (n == 0) {
        lv_obj_t *l = lv_label_create(s_wifi_list);
        lv_label_set_text(l, "no network found, tap RESCAN");
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0x8A939E), 0);
    } else {
        /* 信号强的排前面 */
        for (int i = 1; i < n; i++) {
            wifi_ap_t t = s_aps[i];
            int j = i - 1;
            while (j >= 0 && s_aps[j].rssi < t.rssi) {
                s_aps[j + 1] = s_aps[j];
                j--;
            }
            s_aps[j + 1] = t;
        }
        for (int i = 0; i < n; i++) {
            lv_obj_t *b = lv_button_create(s_wifi_list);
            lv_obj_set_size(b, 650, 38);
            lv_obj_set_style_radius(b, 8, 0);
            lv_obj_set_style_bg_color(b, lv_color_hex(0x222831), 0);
            lv_obj_set_style_border_width(b, 0, 0);
            lv_obj_set_style_shadow_width(b, 0, 0);
            lv_obj_add_event_cb(b, ap_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

            lv_obj_t *l = lv_label_create(b);
            lv_label_set_text_fmt(l, "%s%s", s_aps[i].ssid, s_aps[i].auth ? "  *" : "");
            lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(l, lv_color_hex(0xE6EDF3), 0);
            lv_obj_align(l, LV_ALIGN_LEFT_MID, 12, 0);

            lv_obj_t *r = lv_label_create(b);
            lv_label_set_text_fmt(r, "%d dBm", s_aps[i].rssi);
            lv_obj_set_style_text_font(r, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(r, lv_color_hex(0x6E757C), 0);
            lv_obj_align(r, LV_ALIGN_RIGHT_MID, -12, 0);
        }
    }
    lvgl_port_unlock();
    vTaskDelete(NULL);
}

static void scan_start_cb(lv_event_t *e)
{
    (void)e;
    list_msg("scanning...");
    xTaskCreate(wifi_scan_task, "wifiscan", 8192, NULL, 3, NULL);
}

static void wifi_build(void)
{
    s_wifi_bd = make_backdrop(wifi_close_cb);
    lv_obj_t *p = make_panel(s_wifi_bd, 720, 440, 8);

    lv_obj_t *t = lv_label_create(p);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0x8AB4F8), 0);
    lv_label_set_text(t, "WI-FI");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 10);

    make_btn(p, 600, 8, 100, 34, "RESCAN", scan_start_cb, 0x242A31, &lv_font_montserrat_14);

    s_wifi_list = lv_obj_create(p);
    lv_obj_set_pos(s_wifi_list, 18, 50);
    lv_obj_set_size(s_wifi_list, 684, 136);
    lv_obj_set_style_bg_color(s_wifi_list, lv_color_hex(0x101418), 0);
    lv_obj_set_style_border_color(s_wifi_list, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_border_width(s_wifi_list, 1, 0);
    lv_obj_set_style_radius(s_wifi_list, 10, 0);
    lv_obj_set_style_pad_all(s_wifi_list, 6, 0);
    lv_obj_set_style_pad_row(s_wifi_list, 6, 0);
    lv_obj_set_flex_flow(s_wifi_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_wifi_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_wifi_list, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *l1 = lv_label_create(p);
    lv_obj_set_style_text_font(l1, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(l1, lv_color_hex(0x9AA4B0), 0);
    lv_label_set_text(l1, "SSID");
    lv_obj_set_pos(l1, 24, 200);

    s_wifi_ssid = lv_textarea_create(p);
    lv_obj_set_pos(s_wifi_ssid, 110, 192);
    lv_obj_set_size(s_wifi_ssid, 560, 46);
    lv_textarea_set_one_line(s_wifi_ssid, true);
    lv_textarea_set_max_length(s_wifi_ssid, 32);
    lv_textarea_set_placeholder_text(s_wifi_ssid, "tap a network above or type here");
    lv_obj_add_event_cb(s_wifi_ssid, wifi_field_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *l2 = lv_label_create(p);
    lv_obj_set_style_text_font(l2, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(l2, lv_color_hex(0x9AA4B0), 0);
    lv_label_set_text(l2, "PASS");
    lv_obj_set_pos(l2, 24, 258);

    s_wifi_pass = lv_textarea_create(p);
    lv_obj_set_pos(s_wifi_pass, 110, 250);
    lv_obj_set_size(s_wifi_pass, 560, 46);
    lv_textarea_set_one_line(s_wifi_pass, true);
    lv_textarea_set_password_mode(s_wifi_pass, true);
    lv_textarea_set_max_length(s_wifi_pass, 64);
    lv_textarea_set_placeholder_text(s_wifi_pass, "password (empty = open)");
    lv_obj_add_event_cb(s_wifi_pass, wifi_field_cb, LV_EVENT_CLICKED, NULL);

    s_wifi_hint = lv_label_create(p);
    lv_obj_set_style_text_font(s_wifi_hint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_wifi_hint, lv_color_hex(0x6FDFA0), 0);
    lv_label_set_text(s_wifi_hint, "");
    lv_obj_set_pos(s_wifi_hint, 24, 308);

    make_btn(p, 380, 340, 140, 46, "SAVE", wifi_save_cb, 0x2B6CB0, &lv_font_montserrat_20);
    make_btn(p, 540, 340, 140, 46, "FORGET", wifi_forget_cb, 0x7A2E2E, &lv_font_montserrat_20);

    s_wifi_kb = lv_keyboard_create(lv_screen_active());
    lv_obj_set_size(s_wifi_kb, 800, 200);
    lv_obj_align(s_wifi_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(s_wifi_kb, wifi_kb_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(s_wifi_kb, LV_OBJ_FLAG_HIDDEN);
}

void settings_open_wifi(void)
{
    lvgl_port_lock(0);
    if (!s_wifi_bd) {
        wifi_build();
    }
    lv_obj_clear_flag(s_wifi_bd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_wifi_bd);
    lv_obj_add_flag(s_wifi_kb, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_wifi_hint, "");
    lvgl_port_unlock();
    list_msg("tap RESCAN to scan");
    scan_start_cb(NULL);
}

/* ================= 时间 ================= */
static lv_obj_t *s_time_bd;
static lv_obj_t *s_t_hour, *s_t_min, *s_t_year, *s_t_mon, *s_t_day;
static int s_h, s_m, s_y, s_mo, s_d;

static void time_close_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_time_bd, LV_OBJ_FLAG_HIDDEN);
}

static void time_step_cb(lv_event_t *e)
{
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    switch (k) {
    case 0: s_h = (s_h + 1) % 24; break;
    case 1: s_h = (s_h + 23) % 24; break;
    case 2: s_m = (s_m + 1) % 60; break;
    case 3: s_m = (s_m + 59) % 60; break;
    case 4: s_y = s_y + 1; break;
    case 5: s_y = s_y - 1; break;
    case 6: s_mo = s_mo % 12 + 1; break;
    case 7: s_mo = (s_mo + 10) % 12 + 1; break;
    case 8: s_d = s_d % 31 + 1; break;
    case 9: s_d = (s_d + 29) % 31 + 1; break;
    }
    lv_label_set_text_fmt(s_t_hour, "%02d", s_h);
    lv_label_set_text_fmt(s_t_min, "%02d", s_m);
    lv_label_set_text_fmt(s_t_year, "%04d", s_y);
    lv_label_set_text_fmt(s_t_mon, "%02d", s_mo);
    lv_label_set_text_fmt(s_t_day, "%02d", s_d);
}

static void time_save_cb(lv_event_t *e)
{
    (void)e;
    struct tm tm = {
        .tm_year = s_y - 1900,
        .tm_mon = s_mo - 1,
        .tm_mday = s_d,
        .tm_hour = s_h,
        .tm_min = s_m,
        .tm_sec = 0,
        .tm_isdst = -1,
    };
    time_t t = mktime(&tm);
    if (t > 0) {
        clock_set_epoch((uint32_t)t);
        ui_clock_refresh_now();
        ESP_LOGI(TAG, "time set manually: %04d-%02d-%02d %02d:%02d", s_y, s_mo, s_d, s_h, s_m);
    }
    lv_obj_add_flag(s_time_bd, LV_OBJ_FLAG_HIDDEN);
}

static void time_build(void)
{
    s_time_bd = make_backdrop(time_close_cb);
    lv_obj_t *p = make_panel(s_time_bd, 620, 400, 40);

    lv_obj_t *t = lv_label_create(p);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0x8AB4F8), 0);
    lv_label_set_text(t, "SET TIME");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 10);

    /* ---- 时 : 分 ---- */
    s_t_hour = lv_label_create(p);
    lv_obj_set_style_text_font(s_t_hour, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_t_hour, lv_color_white(), 0);
    lv_obj_set_width(s_t_hour, 100);
    lv_obj_set_style_text_align(s_t_hour, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_t_hour, "00");
    lv_obj_set_pos(s_t_hour, 150, 86);

    lv_obj_t *colon = lv_label_create(p);
    lv_obj_set_style_text_font(colon, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(colon, lv_color_hex(0x8A939E), 0);
    lv_label_set_text(colon, ":");
    lv_obj_set_pos(colon, 258, 86);

    s_t_min = lv_label_create(p);
    lv_obj_set_style_text_font(s_t_min, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_t_min, lv_color_white(), 0);
    lv_obj_set_width(s_t_min, 100);
    lv_obj_set_style_text_align(s_t_min, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_t_min, "00");
    lv_obj_set_pos(s_t_min, 290, 86);

    make_btn(p, 160, 54, 80, 28, LV_SYMBOL_UP, time_step_cb, 0x242A31, &lv_font_montserrat_14);
    make_btn(p, 160, 156, 80, 28, LV_SYMBOL_DOWN, time_step_cb, 0x242A31, &lv_font_montserrat_14);
    make_btn(p, 300, 54, 80, 28, LV_SYMBOL_UP, time_step_cb, 0x242A31, &lv_font_montserrat_14);
    make_btn(p, 300, 156, 80, 28, LV_SYMBOL_DOWN, time_step_cb, 0x242A31, &lv_font_montserrat_14);

    /* ---- 年 / 月 / 日 ---- */
    s_t_year = lv_label_create(p);
    lv_obj_set_style_text_font(s_t_year, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_t_year, lv_color_hex(0xD9DEE4), 0);
    lv_obj_set_width(s_t_year, 140);
    lv_obj_set_style_text_align(s_t_year, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_t_year, "2026");
    lv_obj_set_pos(s_t_year, 40, 212);

    s_t_mon = lv_label_create(p);
    lv_obj_set_style_text_font(s_t_mon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_t_mon, lv_color_hex(0xD9DEE4), 0);
    lv_obj_set_width(s_t_mon, 110);
    lv_obj_set_style_text_align(s_t_mon, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_t_mon, "01");
    lv_obj_set_pos(s_t_mon, 235, 212);

    s_t_day = lv_label_create(p);
    lv_obj_set_style_text_font(s_t_day, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_t_day, lv_color_hex(0xD9DEE4), 0);
    lv_obj_set_width(s_t_day, 110);
    lv_obj_set_style_text_align(s_t_day, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_t_day, "01");
    lv_obj_set_pos(s_t_day, 405, 212);

    make_btn(p, 55, 248, 50, 32, "+", time_step_cb, 0x242A31, &lv_font_montserrat_20);
    make_btn(p, 115, 248, 50, 32, "-", time_step_cb, 0x242A31, &lv_font_montserrat_20);
    make_btn(p, 242, 248, 44, 32, "+", time_step_cb, 0x242A31, &lv_font_montserrat_20);
    make_btn(p, 292, 248, 44, 32, "-", time_step_cb, 0x242A31, &lv_font_montserrat_20);
    make_btn(p, 412, 248, 44, 32, "+", time_step_cb, 0x242A31, &lv_font_montserrat_20);
    make_btn(p, 462, 248, 44, 32, "-", time_step_cb, 0x242A31, &lv_font_montserrat_20);

    make_btn(p, 150, 312, 140, 46, "SAVE", time_save_cb, 0x2B6CB0, &lv_font_montserrat_20);
    make_btn(p, 330, 312, 140, 46, "CANCEL", time_close_cb, 0x7A2E2E, &lv_font_montserrat_20);
}

void settings_open_time(void)
{
    lvgl_port_lock(0);
    if (!s_time_bd) {
        time_build();
    }
    struct tm tm;
    if (clock_get(&tm) == 0) {
        s_h = tm.tm_hour;
        s_m = tm.tm_min;
        s_y = tm.tm_year + 1900;
        s_mo = tm.tm_mon + 1;
        s_d = tm.tm_mday;
    } else {
        s_h = 12;
        s_m = 0;
        s_y = 2026;
        s_mo = 1;
        s_d = 1;
    }
    lv_label_set_text_fmt(s_t_hour, "%02d", s_h);
    lv_label_set_text_fmt(s_t_min, "%02d", s_m);
    lv_label_set_text_fmt(s_t_year, "%04d", s_y);
    lv_label_set_text_fmt(s_t_mon, "%02d", s_mo);
    lv_label_set_text_fmt(s_t_day, "%02d", s_d);

    lv_obj_clear_flag(s_time_bd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_time_bd);
    lvgl_port_unlock();
}

void settings_init(void)
{
    /* 面板在首次打开时创建，这里什么都不用做 */
}

/* ================= 位置 ================= */
static lv_obj_t *s_loc_bd;
static lv_obj_t *s_loc_hint;

typedef struct {
    const char *name;
    float lat;
    float lon;
} city_t;

static const city_t s_cities[] = {
    { "上海", 31.2304f, 121.4737f }, { "北京", 39.9042f, 116.4074f },
    { "广州", 23.1291f, 113.2644f }, { "深圳", 22.5431f, 114.0579f },
    { "杭州", 30.2741f, 120.1551f }, { "南京", 32.0603f, 118.7969f },
    { "成都", 30.5728f, 104.0668f }, { "武汉", 30.5928f, 114.3055f },
    { "西安", 34.3416f, 108.9398f }, { "天津", 39.0842f, 117.2009f },
    { "苏州", 31.2989f, 120.5853f }, { "青岛", 36.0671f, 120.3826f },
    { "长沙", 28.2282f, 112.9388f }, { "郑州", 34.7466f, 113.6254f },
    { "沈阳", 41.8057f, 123.4315f }, { "大连", 38.9140f, 121.6147f },
    { "厦门", 24.4798f, 118.0894f }, { "昆明", 25.0389f, 102.7183f },
    { "合肥", 31.8206f, 117.2272f }, { "福州", 26.0745f, 119.2965f },
    { "济南", 36.6512f, 117.1201f }, { "哈尔滨", 45.8038f, 126.5350f },
    { "长春", 43.8171f, 125.3235f }, { "石家庄", 38.0428f, 114.5149f },
    { "太原", 37.8706f, 112.5489f }, { "南昌", 28.6820f, 115.8579f },
    { "贵阳", 26.6470f, 106.6302f }, { "南宁", 22.8170f, 108.3665f },
    { "兰州", 36.0611f, 103.8343f }, { "乌鲁木齐", 43.8256f, 87.6168f },
    { "拉萨", 29.6520f, 91.1721f }, { "海口", 20.0440f, 110.1999f },
    { "台北", 25.0330f, 121.5654f }, { "香港", 22.3193f, 114.1694f },
    { "澳门", 22.1987f, 113.5439f },
};

static void loc_close_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_loc_bd, LV_OBJ_FLAG_HIDDEN);
}

static void loc_auto_cb(lv_event_t *e)
{
    (void)e;
    weather_set_auto_location(true);
    lv_label_set_text(s_loc_hint, "locating...");
}

static void loc_manual_cb(lv_event_t *e)
{
    (void)e;
    weather_set_auto_location(false);
    lv_label_set_text(s_loc_hint, "pick a city");
}

static void apply_location_task(void *arg)
{
    int idx = (int)(intptr_t)arg;
    weather_set_location(s_cities[idx].lat, s_cities[idx].lon);
    weather_set_location_name(s_cities[idx].name);
    weather_refresh_now();
    ESP_LOGI(TAG, "location applied: %s", s_cities[idx].name);
    vTaskDelete(NULL);
}

static void city_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    char buf[64];
    snprintf(buf, sizeof(buf), "saved: %s", s_cities[idx].name);
    lv_label_set_text(s_loc_hint, buf);
    ESP_LOGI(TAG, "city tapped: %s", s_cities[idx].name);
    xTaskCreate(apply_location_task, "setloc", 4096, (void *)(intptr_t)idx, 3, NULL);
}

static void loc_build(void)
{
    s_loc_bd = make_backdrop(loc_close_cb);
    lv_obj_t *p = make_panel(s_loc_bd, 720, 400, 20);

    lv_obj_t *t = lv_label_create(p);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0x8AB4F8), 0);
    lv_label_set_text(t, "LOCATION");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 10);

    make_btn(p, 40, 44, 170, 44, "AUTO (IP)", loc_auto_cb, 0x2E7D4F, &lv_font_montserrat_20);
    make_btn(p, 230, 44, 170, 44, "MANUAL", loc_manual_cb, 0x2B6CB0, &lv_font_montserrat_20);

    s_loc_hint = lv_label_create(p);
    lv_obj_set_style_text_font(s_loc_hint, &font_cjk36, 0);
    lv_obj_set_style_text_color(s_loc_hint, lv_color_hex(0x9AA4B0), 0);
    lv_label_set_text(s_loc_hint, "");
    lv_obj_set_width(s_loc_hint, 270);
    lv_obj_set_style_text_align(s_loc_hint, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_loc_hint, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(s_loc_hint, 423, 44);

    lv_obj_t *list = lv_obj_create(p);
    lv_obj_set_pos(list, 20, 98);
    lv_obj_set_size(list, 680, 226);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x101418), 0);
    lv_obj_set_style_border_color(list, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_border_width(list, 1, 0);
    lv_obj_set_style_radius(list, 10, 0);
    lv_obj_set_style_pad_all(list, 6, 0);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_style_pad_column(list, 6, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    for (int i = 0; i < (int)(sizeof(s_cities) / sizeof(s_cities[0])); i++) {
        lv_obj_t *b = lv_button_create(list);
        lv_obj_set_size(b, 160, 46);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x222831), 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_add_event_cb(b, city_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, s_cities[i].name);
        lv_obj_set_style_text_font(l, &font_cjk, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0xE6EDF3), 0);
        lv_obj_center(l);
    }

    make_btn(p, 290, 338, 140, 46, "CLOSE", loc_close_cb, 0x7A2E2E, &lv_font_montserrat_20);
}

void settings_open_location(void)
{
    lvgl_port_lock(0);
    if (!s_loc_bd) {
        loc_build();
    }
    char buf[72];
    snprintf(buf, sizeof(buf), "%s  %.4f, %.4f",
             weather_auto_location() ? "AUTO" : "MANUAL",
             weather_latitude(), weather_longitude());
    lv_label_set_text(s_loc_hint, buf);
    lv_obj_clear_flag(s_loc_bd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_loc_bd);
    lvgl_port_unlock();
}

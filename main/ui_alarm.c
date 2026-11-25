#include "ui_alarm.h"

#include <stdio.h>

#include "alarm.h"
#include "esp_lvgl_port.h"

LV_FONT_DECLARE(font_digit_small);

static lv_obj_t *s_backdrop;
static lv_obj_t *s_hour;
static lv_obj_t *s_minute;
static lv_obj_t *s_on_lbl;
static lv_obj_t *s_on_btn;

static int s_h = 7;
static int s_m = 0;
static bool s_on;

static void refresh(void)
{
    lv_label_set_text_fmt(s_hour, "%02d", s_h);
    lv_label_set_text_fmt(s_minute, "%02d", s_m);
    lv_label_set_text(s_on_lbl, s_on ? "ON" : "OFF");
    lv_obj_set_style_bg_color(s_on_btn, lv_color_hex(s_on ? 0x2E7D4F : 0x2A2F36), 0);
}

static void close_panel(void)
{
    lv_obj_add_flag(s_backdrop, LV_OBJ_FLAG_HIDDEN);
}

static void step_cb(lv_event_t *e)
{
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    switch (k) {
    case 0: s_h = (s_h + 1) % 24; break;
    case 1: s_h = (s_h + 23) % 24; break;
    case 2: s_m = (s_m + 1) % 60; break;
    case 3: s_m = (s_m + 59) % 60; break;
    }
    refresh();
}

static void on_toggle_cb(lv_event_t *e)
{
    (void)e;
    s_on = !s_on;
    refresh();
}

static void save_cb(lv_event_t *e)
{
    (void)e;
    if (s_on) {
        alarm_set(s_h, s_m);
    } else {
        alarm_clear();
    }
    close_panel();
}

static void clear_cb(lv_event_t *e)
{
    (void)e;
    alarm_clear();
    close_panel();
}

static void backdrop_cb(lv_event_t *e)
{
    (void)e;
    close_panel();
}

static lv_obj_t *make_btn(lv_obj_t *parent, int x, int y, int w, int h, const char *txt,
                          lv_event_cb_t cb, void *ud, uint32_t color, const lv_font_t *font)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ud);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_font(lbl, font ? font : &lv_font_montserrat_20, 0);
    lv_obj_center(lbl);
    return btn;
}

void alarm_ui_init(void)
{
    lvgl_port_lock(0);

    lv_obj_t *scr = lv_screen_active();

    s_backdrop = lv_obj_create(scr);
    lv_obj_set_pos(s_backdrop, 0, 0);
    lv_obj_set_size(s_backdrop, 800, 480);
    lv_obj_set_style_bg_color(s_backdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_backdrop, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_backdrop, 0, 0);
    lv_obj_set_style_pad_all(s_backdrop, 0, 0);
    lv_obj_clear_flag(s_backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_backdrop, backdrop_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *panel = lv_obj_create(s_backdrop);
    lv_obj_set_pos(panel, 120, 40);
    lv_obj_set_size(panel, 560, 400);
    lv_obj_set_style_radius(panel, 20, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x15181C), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x2A2F36), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *title = lv_label_create(panel);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE6EDF3), 0);
    lv_label_set_text(title, "ALARM");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    /* 小时 */
    s_hour = lv_label_create(panel);
    lv_obj_set_style_text_font(s_hour, &font_digit_small, 0);
    lv_obj_set_style_text_color(s_hour, lv_color_white(), 0);
    lv_obj_set_width(s_hour, 140);
    lv_obj_set_style_text_align(s_hour, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_hour, 100, 118);
    lv_label_set_text(s_hour, "07");

    lv_obj_t *colon = lv_label_create(panel);
    lv_obj_set_style_text_font(colon, &font_digit_small, 0);
    lv_obj_set_style_text_color(colon, lv_color_hex(0x8A939E), 0);
    lv_label_set_text(colon, ":");
    lv_obj_set_pos(colon, 258, 118);

    /* 分钟 */
    s_minute = lv_label_create(panel);
    lv_obj_set_style_text_font(s_minute, &font_digit_small, 0);
    lv_obj_set_style_text_color(s_minute, lv_color_white(), 0);
    lv_obj_set_width(s_minute, 140);
    lv_obj_set_style_text_align(s_minute, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_minute, 320, 118);
    lv_label_set_text(s_minute, "00");

    /* 加减按钮 */
    make_btn(panel, 130, 74, 80, 40, LV_SYMBOL_UP, step_cb, (void *)0, 0x242A31, &lv_font_montserrat_20);
    make_btn(panel, 130, 246, 80, 40, LV_SYMBOL_DOWN, step_cb, (void *)1, 0x242A31, &lv_font_montserrat_20);
    make_btn(panel, 350, 74, 80, 40, LV_SYMBOL_UP, step_cb, (void *)2, 0x242A31, &lv_font_montserrat_20);
    make_btn(panel, 350, 246, 80, 40, LV_SYMBOL_DOWN, step_cb, (void *)3, 0x242A31, &lv_font_montserrat_20);

    /* 开关 / 保存 / 清除 */
    s_on_btn = make_btn(panel, 40, 330, 120, 44, "ON", on_toggle_cb, NULL, 0x2E7D4F, &lv_font_montserrat_20);
    s_on_lbl = lv_obj_get_child(s_on_btn, 0);

    make_btn(panel, 220, 330, 120, 44, "SAVE", save_cb, NULL, 0x2B6CB0, &lv_font_montserrat_20);
    make_btn(panel, 400, 330, 120, 44, "CLEAR", clear_cb, NULL, 0x7A2E2E, &lv_font_montserrat_20);

    lv_obj_add_flag(s_backdrop, LV_OBJ_FLAG_HIDDEN);

    lvgl_port_unlock();
}

void alarm_ui_open(int hour, int minute, bool enabled)
{
    s_h = (hour >= 0) ? hour : 7;
    s_m = (minute >= 0) ? minute : 0;
    s_on = enabled;

    lvgl_port_lock(0);
    refresh();
    lv_obj_clear_flag(s_backdrop, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_backdrop);
    lvgl_port_unlock();
}

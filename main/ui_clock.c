#include "ui_clock.h"

#include <stdio.h>
#include <string.h>

#include "alarm.h"
#include "clock.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lunar.h"
#include "ui_alarm.h"
#include "ui_analog.h"
#include "ui_settings.h"
#include "weather.h"
#include "wifi_time.h"

LV_FONT_DECLARE(font_digit);
LV_FONT_DECLARE(font_digit_small);
LV_FONT_DECLARE(font_cjk);

#define C_BG       0x0A0B0C
#define C_BIG_TOP  0x1C1D1F
#define C_BIG_BOT  0x18191B
#define C_BIG_FG   0xFFFFFF
#define C_SM_TOP   0x161719
#define C_SM_BOT   0x121315
#define C_SM_FG    0x9BA0A6
#define C_SPLIT    0x26272A
#define C_HINGE    0x3A3D40
#define C_SHADOW   0x151618

#define BIG_W  124
#define BIG_H  300
#define BIG_Y  60
#define SM_W   72
#define SM_H   160
#define SM_Y   130

#define RENDER_MAX_W 124
#define RENDER_MAX_H 300

static const int s_big_x[4] = { 32, 166, 324, 458 };
static const int s_sm_x[2] = { 616, 696 };

typedef struct {
    lv_obj_t *card;
    lv_obj_t *img_top;
    lv_obj_t *img_bot;
    lv_obj_t *flap_top;
    lv_obj_t *flap_bot;
    lv_image_dsc_t d_old_top;
    lv_image_dsc_t d_old_bot;
    lv_image_dsc_t d_new_top;
    lv_image_dsc_t d_new_bot;
    uint16_t *b_old_top;
    uint16_t *b_old_bot;
    uint16_t *b_new_top;
    uint16_t *b_new_bot;
    int w;
    int h;
    int half;
    const lv_font_t *font;
    uint32_t fg;
    uint32_t bg_top;
    uint32_t bg_bot;
    int value;
} flip_t;

static flip_t s_flip[6];
static lv_obj_t *s_date;
static lv_obj_t *s_week;
static lv_obj_t *s_lunar;
static lv_obj_t *s_status;
static lv_obj_t *s_alarm;
static lv_obj_t *s_weather;
static lv_obj_t *s_wsep;
static lv_obj_t *s_page2;
static lv_obj_t *s_p2_time;
static lv_obj_t *s_p2_date;
static lv_obj_t *s_p2_weather;
static lv_obj_t *s_p2_loc;
static lv_obj_t *s_p2_alarm;
static lv_obj_t *s_p2_net;
static lv_obj_t *s_overlay;
static lv_obj_t *s_overlay_time;
static lv_obj_t *s_hint;
static lv_obj_t *s_canvas;
static uint16_t *s_render;
static volatile bool s_force;
static volatile bool s_ui_dirty;

static void alarm_chip_cb(lv_event_t *e);
static void show_page2(void);
static void hide_page2(void);
static void tap_cb(lv_event_t *e);

static uint16_t to565(uint32_t c)
{
    return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static void dsc_init(lv_image_dsc_t *d, uint16_t *buf, int w, int h)
{
    memset(d, 0, sizeof(*d));
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_RGB565;
    d->header.w = w;
    d->header.h = h;
    d->data = (const uint8_t *)buf;
    d->data_size = (uint32_t)w * h * 2;
}

static void fill(uint16_t *buf, uint16_t color, int n)
{
    for (int i = 0; i < n; i++) {
        buf[i] = color;
    }
}

static void render_digit(flip_t *f, int digit, uint16_t *top, uint16_t *bot)
{
    fill(s_render, to565(f->bg_top), f->w * f->half);
    fill(s_render + f->w * f->half, to565(f->bg_bot), f->w * f->half);

    if (digit >= 0) {
        char txt[2] = { (char)('0' + digit), '\0' };
        lv_canvas_set_buffer(s_canvas, s_render, f->w, f->h, LV_COLOR_FORMAT_RGB565);

        lv_layer_t layer;
        lv_canvas_init_layer(s_canvas, &layer);

        lv_draw_label_dsc_t dsc;
        lv_draw_label_dsc_init(&dsc);
        dsc.text = txt;
        dsc.font = f->font;
        dsc.color = lv_color_hex(f->fg);
        dsc.align = LV_TEXT_ALIGN_CENTER;

        const int lh = f->font->line_height;
        lv_area_t area = {
            .x1 = 0,
            .y1 = (f->h - lh) / 2,
            .x2 = f->w - 1,
            .y2 = (f->h - lh) / 2 + lh - 1,
        };
        lv_draw_label(&layer, &dsc, &area);
        lv_canvas_finish_layer(s_canvas, &layer);
    }

    memcpy(top, s_render, f->w * f->half * 2);
    memcpy(bot, s_render + f->w * f->half, f->w * f->half * 2);
}

static void anim_scale_y(void *var, int32_t v)
{
    lv_image_set_scale_y((lv_obj_t *)var, v);
}

static void flip_done_cb(lv_anim_t *a)
{
    flip_t *f = (flip_t *)lv_anim_get_user_data(a);
    if (!f) {
        return;
    }
    lv_obj_add_flag(f->flap_top, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(f->flap_bot, LV_OBJ_FLAG_HIDDEN);
    lv_image_set_scale_y(f->flap_top, 256);
    lv_image_set_scale_y(f->flap_bot, 256);

    lv_image_set_src(f->img_bot, &f->d_new_bot);
    memcpy(f->b_old_top, f->b_new_top, f->w * f->half * 2);
    memcpy(f->b_old_bot, f->b_new_bot, f->w * f->half * 2);
    lv_obj_invalidate(f->img_top);
    lv_obj_invalidate(f->img_bot);
}

static void flip_to(flip_t *f, int digit)
{
    if (digit == f->value) {
        return;
    }

    render_digit(f, digit, f->b_new_top, f->b_new_bot);

    lv_image_set_src(f->img_top, &f->d_new_top);
    lv_image_set_src(f->flap_top, &f->d_old_top);
    lv_obj_clear_flag(f->flap_top, LV_OBJ_FLAG_HIDDEN);
    lv_image_set_pivot(f->flap_top, f->w / 2, f->half);
    lv_image_set_scale_y(f->flap_top, 256);

    lv_image_set_src(f->flap_bot, &f->d_new_bot);
    lv_obj_clear_flag(f->flap_bot, LV_OBJ_FLAG_HIDDEN);
    lv_image_set_pivot(f->flap_bot, f->w / 2, 0);
    lv_image_set_scale_y(f->flap_bot, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, f->flap_top);
    lv_anim_set_exec_cb(&a, anim_scale_y);
    lv_anim_set_values(&a, 256, 0);
    lv_anim_set_duration(&a, 170);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_start(&a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, f->flap_bot);
    lv_anim_set_exec_cb(&a, anim_scale_y);
    lv_anim_set_values(&a, 0, 256);
    lv_anim_set_duration(&a, 170);
    lv_anim_set_delay(&a, 160);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_user_data(&a, f);
    lv_anim_set_ready_cb(&a, flip_done_cb);
    lv_anim_start(&a);

    f->value = digit;
}

static void card_create(int idx, int x, int y, int w, int h, const lv_font_t *font,
                        uint32_t fg, uint32_t bg_top, uint32_t bg_bot)
{
    flip_t *f = &s_flip[idx];
    f->w = w;
    f->h = h;
    f->half = h / 2;
    f->font = font;
    f->fg = fg;
    f->bg_top = bg_top;
    f->bg_bot = bg_bot;

    const size_t bytes = (size_t)w * f->half * 2;
    f->b_old_top = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    f->b_old_bot = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    f->b_new_top = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    f->b_new_bot = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    dsc_init(&f->d_old_top, f->b_old_top, w, f->half);
    dsc_init(&f->d_old_bot, f->b_old_bot, w, f->half);
    dsc_init(&f->d_new_top, f->b_new_top, w, f->half);
    dsc_init(&f->d_new_bot, f->b_new_bot, w, f->half);

    render_digit(f, -1, f->b_old_top, f->b_old_bot);

    lv_obj_t *shadow = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(shadow, x - 2, y + 3);
    lv_obj_set_size(shadow, w + 4, h + 4);
    lv_obj_set_style_radius(shadow, 20, 0);
    lv_obj_set_style_bg_color(shadow, lv_color_hex(C_SHADOW), 0);
    lv_obj_set_style_border_width(shadow, 0, 0);
    lv_obj_set_style_pad_all(shadow, 0, 0);
    lv_obj_clear_flag(shadow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(shadow, LV_OBJ_FLAG_CLICKABLE);

    f->card = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(f->card, x, y);
    lv_obj_set_size(f->card, w, h);
    lv_obj_set_style_radius(f->card, 18, 0);
    lv_obj_set_style_bg_color(f->card, lv_color_hex(bg_bot), 0);
    lv_obj_set_style_border_width(f->card, 0, 0);
    lv_obj_set_style_pad_all(f->card, 0, 0);
    lv_obj_set_style_clip_corner(f->card, true, 0);
    lv_obj_clear_flag(f->card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(f->card, LV_OBJ_FLAG_CLICKABLE);

    f->img_top = lv_image_create(f->card);
    lv_obj_set_pos(f->img_top, 0, 0);
    lv_image_set_src(f->img_top, &f->d_old_top);

    f->img_bot = lv_image_create(f->card);
    lv_obj_set_pos(f->img_bot, 0, f->half);
    lv_image_set_src(f->img_bot, &f->d_old_bot);

    lv_obj_t *line = lv_obj_create(f->card);
    lv_obj_set_pos(line, 0, f->half - 1);
    lv_obj_set_size(line, w, 2);
    lv_obj_set_style_bg_color(line, lv_color_hex(C_SPLIT), 0);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);

    f->flap_top = lv_image_create(f->card);
    lv_obj_set_pos(f->flap_top, 0, 0);
    lv_image_set_src(f->flap_top, &f->d_old_top);
    lv_obj_add_flag(f->flap_top, LV_OBJ_FLAG_HIDDEN);

    f->flap_bot = lv_image_create(f->card);
    lv_obj_set_pos(f->flap_bot, 0, f->half);
    lv_image_set_src(f->flap_bot, &f->d_new_bot);
    lv_obj_add_flag(f->flap_bot, LV_OBJ_FLAG_HIDDEN);

    /* 铰链小钉 */
    for (int s = 0; s < 2; s++) {
        lv_obj_t *pin = lv_obj_create(f->card);
        lv_obj_set_pos(pin, s ? w - 4 : 2, f->half - 6);
        lv_obj_set_size(pin, 2, 12);
        lv_obj_set_style_bg_color(pin, lv_color_hex(C_HINGE), 0);
        lv_obj_set_style_radius(pin, 1, 0);
        lv_obj_set_style_border_width(pin, 0, 0);
        lv_obj_set_style_pad_all(pin, 0, 0);
        lv_obj_clear_flag(pin, LV_OBJ_FLAG_CLICKABLE);
    }

    f->value = -1;
}

static void clock_task(void *arg)
{
    (void)arg;
    char last_date[64] = "";
    char last_wifi[48] = "";
    int last[6] = { -2, -2, -2, -2, -2, -2 };

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));

        struct tm tm;
        bool ok = (clock_get(&tm) == 0);

        char date[64], week[32], lun[40];
        int d[6] = { -1, -1, -1, -1, -1, -1 };

        if (ok) {
            snprintf(date, sizeof(date), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
            snprintf(week, sizeof(week), "%s", weekday_cn(tm.tm_wday));
            lunar_date_t ld;
            if (lunar_from_solar(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, &ld)) {
                char tmp[24];
                lunar_format(&ld, tmp, sizeof(tmp));
                snprintf(lun, sizeof(lun), "农历%s", tmp);
            } else {
                snprintf(lun, sizeof(lun), "农历--");
            }
            d[0] = tm.tm_hour / 10;
            d[1] = tm.tm_hour % 10;
            d[2] = tm.tm_min / 10;
            d[3] = tm.tm_min % 10;
            d[4] = tm.tm_sec / 10;
            d[5] = tm.tm_sec % 10;
        } else {
            snprintf(date, sizeof(date), "---- -- --");
            snprintf(week, sizeof(week), "---");
            snprintf(lun, sizeof(lun), "农历--");
        }

        if (s_force) {
            s_force = false;
            for (int i = 0; i < 6; i++) {
                last[i] = -2;
            }
            last_date[0] = '\0';
        }

        bool changed = (strcmp(date, last_date) != 0);
        for (int i = 0; i < 6; i++) {
            if (d[i] != last[i]) {
                changed = true;
            }
        }
        const char *wst = wifi_time_synced() ? "SNTP" : (wifi_time_connected() ? "WiFi" : "OFF");
        const char *wx = weather_text();
        char right[48];
        if (wx[0]) {
            snprintf(right, sizeof(right), "%s", wx);
        } else {
            snprintf(right, sizeof(right), "%s", wst);
        }
        if (strcmp(right, last_wifi) != 0) {
            changed = true;
        }
        if (s_ui_dirty) {
            s_ui_dirty = false;
            changed = true;
        }
        if (!changed) {
            continue;
        }

        lvgl_port_lock(0);
        lv_label_set_text(s_date, date);
        lv_label_set_text(s_week, week);
        lv_label_set_text(s_lunar, lun);
        lv_obj_clear_flag(s_status, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_status, wst);
        if (wx[0]) {
            lv_label_set_text(s_weather, wx);
            lv_obj_clear_flag(s_weather, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_wsep, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_weather, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_wsep, LV_OBJ_FLAG_HIDDEN);
        }
        if (ok) {
            lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        }
        for (int i = 0; i < 6; i++) {
            if (d[i] != last[i]) {
                flip_to(&s_flip[i], d[i]);
                last[i] = d[i];
            }
        }
        lvgl_port_unlock();

        snprintf(last_date, sizeof(last_date), "%s", date);
        snprintf(last_wifi, sizeof(last_wifi), "%s", right);
    }
}

void ui_clock_init(lv_display_t *disp)
{
    lvgl_port_lock(0);

    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(0xE6EDF3), 0);

    s_render = heap_caps_malloc(RENDER_MAX_W * RENDER_MAX_H * 2, MALLOC_CAP_SPIRAM);
    s_canvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(s_canvas, s_render, RENDER_MAX_W, RENDER_MAX_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);

    /* 整屏透明点击层：点空白处进 INFO 页 */
    lv_obj_t *tap = lv_obj_create(scr);
    lv_obj_set_pos(tap, 0, 0);
    lv_obj_set_size(tap, 800, 480);
    lv_obj_set_style_bg_opa(tap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tap, 0, 0);
    lv_obj_set_style_pad_all(tap, 0, 0);
    lv_obj_clear_flag(tap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tap, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tap, tap_cb, LV_EVENT_CLICKED, NULL);

    for (int i = 0; i < 4; i++) {
        card_create(i, s_big_x[i], BIG_Y, BIG_W, BIG_H, &font_digit,
                    0xFFFFFF, 0x1C1D1F, 0x18191B);
    }
    for (int i = 0; i < 2; i++) {
        card_create(4 + i, s_sm_x[i], SM_Y, SM_W, SM_H, &font_digit_small,
                    0x9BA0A6, 0x161719, 0x121315);
    }

    /* 冒号 */
    const int colons[2] = { 307, 599 };
    for (int c = 0; c < 2; c++) {
        for (int i = 0; i < 2; i++) {
            lv_obj_t *dot = lv_obj_create(scr);
            lv_obj_set_pos(dot, colons[c] - 5, 190 + i * 40);
            lv_obj_set_size(dot, 10, 10);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, lv_color_hex(0x6E6E6E), 0);
            lv_obj_set_style_border_width(dot, 0, 0);
        }
    }

    /* 底部：星期三 · 2026-11-25 · 农历十月十七（居中一行） */
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_pos(bar, 0, 394);
    lv_obj_set_size(bar, 800, 46);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_pad_column(bar, 12, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *topbar = lv_obj_create(scr);
    lv_obj_set_size(topbar, 340, 46);
    lv_obj_set_style_bg_opa(topbar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(topbar, 0, 0);
    lv_obj_set_style_pad_all(topbar, 0, 0);
    lv_obj_set_style_pad_column(topbar, 14, 0);
    lv_obj_set_flex_flow(topbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topbar, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(topbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(topbar, LV_ALIGN_TOP_RIGHT, -20, 8);

    lv_obj_t *alarm_btn = lv_button_create(topbar);
    lv_obj_set_size(alarm_btn, 132, 42);
    lv_obj_set_style_radius(alarm_btn, 9, 0);
    lv_obj_set_style_bg_opa(alarm_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(alarm_btn, 0, 0);
    lv_obj_set_style_shadow_width(alarm_btn, 0, 0);
    lv_obj_add_event_cb(alarm_btn, alarm_chip_cb, LV_EVENT_CLICKED, NULL);

    s_alarm = lv_label_create(alarm_btn);
    lv_obj_set_style_text_font(s_alarm, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_alarm, lv_color_hex(0x9AA4B0), 0);
    lv_label_set_text(s_alarm, "ALARM");
    lv_obj_center(s_alarm);

    s_date = lv_label_create(bar);
    lv_obj_set_style_text_font(s_date, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_date, lv_color_hex(0x6FA8FF), 0);
    lv_label_set_text(s_date, "---- -- --");

    s_week = lv_label_create(bar);
    lv_obj_set_style_text_font(s_week, &font_cjk, 0);
    lv_obj_set_style_text_color(s_week, lv_color_hex(0x6FDFA0), 0);
    lv_label_set_text(s_week, "---");

    s_lunar = lv_label_create(bar);
    lv_obj_set_style_text_font(s_lunar, &font_cjk, 0);
    lv_obj_set_style_text_color(s_lunar, lv_color_hex(0xFFCB5C), 0);
    lv_label_set_text(s_lunar, "农历--");

    lv_obj_t *sep2 = lv_obj_create(bar);
    s_wsep = sep2;
    lv_obj_set_size(sep2, 5, 5);
    lv_obj_set_style_radius(sep2, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(sep2, lv_color_hex(0x5A6066), 0);
    lv_obj_set_style_border_width(sep2, 0, 0);
    lv_obj_clear_flag(sep2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(sep2, LV_OBJ_FLAG_HIDDEN);

    s_weather = lv_label_create(bar);
    lv_obj_set_style_text_font(s_weather, &font_cjk, 0);
    lv_obj_set_style_text_color(s_weather, lv_color_hex(0xD9DEE4), 0);
    lv_label_set_text(s_weather, "");
    lv_obj_add_flag(s_weather, LV_OBJ_FLAG_HIDDEN);

    s_status = lv_label_create(topbar);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x5C636B), 0);
    lv_label_set_text(s_status, "OFF");


    s_hint = lv_label_create(scr);
    lv_obj_set_style_text_font(s_hint, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(0x6E757C), 0);
    lv_label_set_text(s_hint, "TIME NOT SET");
    lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, BIG_Y + BIG_H / 2 - 10);

    lvgl_port_unlock();

    xTaskCreate(clock_task, "clock", 6144, NULL, 4, NULL);
}

void ui_clock_refresh_now(void)
{
    s_force = true;
}

void ui_clock_refresh_weather(void)
{
    s_ui_dirty = true;
}

static void alarm_chip_cb(lv_event_t *e)
{
    (void)e;
    int h = -1, m = -1;
    bool on = alarm_get(&h, &m);
    alarm_ui_open(h, m, on);
}

static void overlay_click_cb(lv_event_t *e)
{
    (void)e;
    alarm_stop();
}

void ui_clock_set_alarm(int hour, int minute, bool enabled)
{
    if (!s_alarm) {
        return;
    }
    lvgl_port_lock(0);
    if (enabled && hour >= 0) {
        lv_label_set_text_fmt(s_alarm, "AL %02d:%02d", hour, minute);
        lv_obj_clear_flag(s_alarm, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(s_alarm, "ALARM");
    }
    lvgl_port_unlock();
}

void ui_clock_show_ringing(bool on, int hour, int minute)
{
    lvgl_port_lock(0);

    if (!s_overlay) {
        lv_obj_t *scr = lv_screen_active();
        s_overlay = lv_obj_create(scr);
        lv_obj_set_pos(s_overlay, 0, 0);
        lv_obj_set_size(s_overlay, 800, 480);
        lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(s_overlay, LV_OPA_80, 0);
        lv_obj_set_style_border_width(s_overlay, 0, 0);
        lv_obj_set_style_pad_all(s_overlay, 0, 0);
        lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_overlay, overlay_click_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *title = lv_label_create(s_overlay);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_label_set_text(title, "ALARM");
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 96);

        s_overlay_time = lv_label_create(s_overlay);
        lv_obj_set_style_text_font(s_overlay_time, &font_digit_small, 0);
        lv_obj_set_style_text_color(s_overlay_time, lv_color_white(), 0);
        lv_label_set_text(s_overlay_time, "00:00");
        lv_obj_align(s_overlay_time, LV_ALIGN_CENTER, 0, -10);

        lv_obj_t *hint = lv_label_create(s_overlay);
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(hint, lv_color_hex(0x9AA4B0), 0);
        lv_label_set_text(hint, "TAP ANYWHERE TO STOP");
        lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -84);
    }

    if (on) {
        lv_label_set_text_fmt(s_overlay_time, "%02d:%02d", hour, minute);
        lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_overlay);
    } else {
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    lvgl_port_unlock();
}

static lv_obj_t *p2_row(lv_obj_t *parent, int y, const char *name, const lv_font_t *vf, lv_obj_t **out_val)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x6E757C), 0);
    lv_label_set_text(lbl, name);
    lv_obj_set_pos(lbl, 90, y);

    lv_obj_t *val = lv_label_create(parent);
    lv_obj_set_style_text_font(val, vf, 0);
    lv_obj_set_style_text_color(val, lv_color_hex(0xE6EDF3), 0);
    lv_label_set_text(val, "--");
    lv_obj_set_pos(val, 300, y);
    if (out_val) {
        *out_val = val;
    }
    return val;
}

static void hide_page2(void)
{
    if (s_page2) {
        ESP_LOGI("ui", "hide info page");
        lv_obj_add_flag(s_page2, LV_OBJ_FLAG_HIDDEN);
    }
}

static void page2_cb(lv_event_t *e)
{
    (void)e;
    hide_page2();
}

static void tap_cb(lv_event_t *e)
{
    (void)e;
    ui_analog_show(true);
}

static void show_page2(void)
{
    if (alarm_is_ringing()) {
        return;
    }
    if (!s_page2) {
        lv_obj_t *scr = lv_screen_active();
        s_page2 = lv_obj_create(scr);
        lv_obj_set_pos(s_page2, 0, 0);
        lv_obj_set_size(s_page2, 800, 480);
        lv_obj_set_style_bg_color(s_page2, lv_color_hex(0x0A0B0C), 0);
        lv_obj_set_style_bg_opa(s_page2, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_page2, 0, 0);
        lv_obj_set_style_pad_all(s_page2, 0, 0);
        lv_obj_clear_flag(s_page2, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(s_page2, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_page2, page2_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *title = lv_label_create(s_page2);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(0x8AB4F8), 0);
        lv_label_set_text(title, "INFO");
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 26);

        p2_row(s_page2, 90, "TIME", &lv_font_montserrat_28, &s_p2_time);
        p2_row(s_page2, 136, "DATE", &font_cjk, &s_p2_date);
        p2_row(s_page2, 182, "WEATHER", &font_cjk, &s_p2_weather);
        p2_row(s_page2, 228, "LOCATION", &font_cjk, &s_p2_loc);
        p2_row(s_page2, 274, "ALARM", &lv_font_montserrat_28, &s_p2_alarm);
        p2_row(s_page2, 320, "NETWORK", &lv_font_montserrat_20, &s_p2_net);

        lv_obj_t *b0 = lv_button_create(s_page2);
        lv_obj_set_pos(b0, 40, 372);
        lv_obj_set_size(b0, 200, 50);
        lv_obj_set_style_radius(b0, 12, 0);
        lv_obj_set_style_bg_color(b0, lv_color_hex(0x6B4FA0), 0);
        lv_obj_set_style_border_width(b0, 0, 0);
        lv_obj_set_style_shadow_width(b0, 0, 0);
        lv_obj_add_event_cb(b0, (lv_event_cb_t)settings_open_location, LV_EVENT_CLICKED, NULL);
        lv_obj_t *l0 = lv_label_create(b0);
        lv_label_set_text(l0, "LOCATION");
        lv_obj_set_style_text_font(l0, &lv_font_montserrat_20, 0);
        lv_obj_center(l0);

        lv_obj_t *b1 = lv_button_create(s_page2);
        lv_obj_set_pos(b1, 290, 372);
        lv_obj_set_size(b1, 200, 50);
        lv_obj_set_style_radius(b1, 12, 0);
        lv_obj_set_style_bg_color(b1, lv_color_hex(0x2B6CB0), 0);
        lv_obj_set_style_border_width(b1, 0, 0);
        lv_obj_set_style_shadow_width(b1, 0, 0);
        lv_obj_add_event_cb(b1, (lv_event_cb_t)settings_open_time, LV_EVENT_CLICKED, NULL);
        lv_obj_t *l1 = lv_label_create(b1);
        lv_label_set_text(l1, "SET TIME");
        lv_obj_set_style_text_font(l1, &lv_font_montserrat_20, 0);
        lv_obj_center(l1);

        lv_obj_t *b2 = lv_button_create(s_page2);
        lv_obj_set_pos(b2, 540, 372);
        lv_obj_set_size(b2, 200, 50);
        lv_obj_set_style_radius(b2, 12, 0);
        lv_obj_set_style_bg_color(b2, lv_color_hex(0x2E7D4F), 0);
        lv_obj_set_style_border_width(b2, 0, 0);
        lv_obj_set_style_shadow_width(b2, 0, 0);
        lv_obj_add_event_cb(b2, (lv_event_cb_t)settings_open_wifi, LV_EVENT_CLICKED, NULL);
        lv_obj_t *l2 = lv_label_create(b2);
        lv_label_set_text(l2, "SET WIFI");
        lv_obj_set_style_text_font(l2, &lv_font_montserrat_20, 0);
        lv_obj_center(l2);

        lv_obj_t *hint = lv_label_create(s_page2);
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(hint, lv_color_hex(0x5C636B), 0);
        lv_label_set_text(hint, "TAP EMPTY AREA TO GO BACK");
        lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);
    }

    char buf[64];
    struct tm tm;
    if (clock_get(&tm) == 0) {
        lv_label_set_text_fmt(s_p2_time, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
        lv_label_set_text_fmt(s_p2_date, "%04d-%02d-%02d %s", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                              weekday_cn(tm.tm_wday));
    } else {
        lv_label_set_text(s_p2_time, "--:--:--");
        lv_label_set_text(s_p2_date, "TIME NOT SET");
    }

    const char *wx = weather_text();
    const char *city = weather_city();
    lv_label_set_text(s_p2_weather, wx[0] ? wx : "--");
    snprintf(buf, sizeof(buf), "%s  (%s)", city[0] ? city : "auto IP",
             weather_auto_location() ? "AUTO" : "MANUAL");
    lv_label_set_text(s_p2_loc, buf);

    int hh = -1, mm = -1;
    if (alarm_get(&hh, &mm)) {
        snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);
    } else {
        snprintf(buf, sizeof(buf), "OFF");
    }
    lv_label_set_text(s_p2_alarm, buf);

    if (wifi_time_connected()) {
        snprintf(buf, sizeof(buf), "%s  %s", wifi_time_synced() ? "SNTP" : "WiFi", wifi_time_ip());
    } else {
        snprintf(buf, sizeof(buf), "OFFLINE");
    }
    lv_label_set_text(s_p2_net, buf);

    lv_obj_clear_flag(s_page2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_page2);
}

static void redraw_timer_cb(lv_timer_t *t)
{
    lv_obj_invalidate(lv_screen_active());
    lv_timer_del(t);
}

void ui_clock_redraw(void)
{
    lvgl_port_lock(0);
    lv_obj_invalidate(lv_screen_active());
    lv_timer_create(redraw_timer_cb, 180, NULL);
    lvgl_port_unlock();
}

void ui_clock_show_info(void)
{
    lvgl_port_lock(0);
    show_page2();
    lvgl_port_unlock();
}

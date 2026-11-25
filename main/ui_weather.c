#include "ui_weather.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "clock.h"
#include "ui_clock.h"
#include "weather.h"

#define AW 800
#define AH 480
#define PI 3.14159265f

LV_FONT_DECLARE(font_temp);
LV_FONT_DECLARE(font_cjk);
LV_FONT_DECLARE(font_cjk20);
LV_FONT_DECLARE(font_cjk26);
LV_FONT_DECLARE(font_cjk36);

static lv_obj_t *s_canvas;
static uint16_t *s_buf;
static bool s_visible;
static char s_big_tmp[16];
static char s_hl_tmp[64];
static char s_hour_lbl[6][16];
static char s_hour_val[6][16];
static char s_lo_tmp[16];
static char s_upd_tmp[40];

static void draw_line(lv_layer_t *l, float x1, float y1, float x2, float y2, int w, lv_color_t c)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.p1.x = (lv_value_precise_t)x1;
    d.p1.y = (lv_value_precise_t)y1;
    d.p2.x = (lv_value_precise_t)x2;
    d.p2.y = (lv_value_precise_t)y2;
    d.color = c;
    d.width = w;
    d.opa = LV_OPA_COVER;
    d.round_start = 1;
    d.round_end = 1;
    lv_draw_line(l, &d);
}

static void draw_dot(lv_layer_t *l, int x, int y, int r, lv_color_t c)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = LV_OPA_COVER;
    d.radius = LV_RADIUS_CIRCLE;
    lv_area_t a = { x - r, y - r, x + r, y + r };
    lv_draw_rect(l, &d, &a);
}

static void draw_rect(lv_layer_t *l, int x1, int y1, int x2, int y2, int r, lv_color_t c)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = LV_OPA_COVER;
    d.radius = r;
    lv_area_t a = { x1, y1, x2, y2 };
    lv_draw_rect(l, &d, &a);
}

static void draw_text(lv_layer_t *l, const char *txt, const lv_font_t *f, lv_color_t c,
                      int x, int y, int w, lv_text_align_t align)
{
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.text = txt;
    d.font = f;
    d.color = c;
    d.align = align;
    lv_area_t a = { x, y, x + w, y + f->line_height };
    lv_draw_label(l, &d, &a);
}

static int text_width(const char *txt, const lv_font_t *f)
{
    lv_point_t sz = { 0, 0 };
    lv_text_get_size(&sz, txt, f, 0, 0, 1000, LV_TEXT_FLAG_NONE);
    return sz.x;
}

/* ---------------- 图标 ---------------- */

static void icon_sun(lv_layer_t *l, int cx, int cy, int s, lv_color_t c)
{
    (void)c;
    draw_dot(l, cx, cy, s / 2, lv_color_hex(0xFFC24A));
    for (int i = 0; i < 8; i++) {
        const float a = (float)i * 45.0f * PI / 180.0f;
        const float x1 = cx + cosf(a) * (s / 2 + 3);
        const float y1 = cy + sinf(a) * (s / 2 + 3);
        const float x2 = cx + cosf(a) * (s / 2 + 8);
        const float y2 = cy + sinf(a) * (s / 2 + 8);
        draw_line(l, x1, y1, x2, y2, 2, lv_color_hex(0xFFA726));
    }
}

static lv_color_t bg_at(int y)
{
    if (y < 0) {
        y = 0;
    }
    if (y > AH - 1) {
        y = AH - 1;
    }
    return lv_color_mix(lv_color_hex(0x6C93B8), lv_color_hex(0x3E6185),
                        (uint8_t)(255 - y * 255 / (AH - 1)));
}

static void icon_moon(lv_layer_t *l, int cx, int cy, int s, lv_color_t c)
{
    (void)c;
    draw_dot(l, cx, cy, s / 2, lv_color_hex(0xF2E2A0));
    draw_dot(l, cx + s / 3, cy - s / 5, s / 2, bg_at(cy));
}

static void icon_cloud(lv_layer_t *l, int cx, int cy, int s, lv_color_t c)
{
    (void)c;
    const lv_color_t cc = lv_color_hex(0xDCE6F0);
    const int r = s / 3;
    draw_dot(l, cx - r, cy + r / 3, r, cc);
    draw_dot(l, cx + r / 4, cy - r / 2, r * 4 / 3, cc);
    draw_dot(l, cx + r * 4 / 3, cy + r / 4, r * 9 / 10, cc);
    draw_rect(l, cx - r * 2, cy + r / 6, cx + r * 3 / 2, cy + r, r / 2, cc);
}

static void icon_rain(lv_layer_t *l, int cx, int cy, int s, lv_color_t c)
{
    icon_cloud(l, cx, cy - 3, s, c);
    for (int i = -1; i <= 1; i++) {
        const int x = cx + i * (s / 3);
        draw_line(l, x, cy + s / 3, x - 3, cy + s / 3 + 7, 2, lv_color_hex(0x6FB6F2));
    }
}

static void icon_snow(lv_layer_t *l, int cx, int cy, int s, lv_color_t c)
{
    icon_cloud(l, cx, cy - 3, s, c);
    for (int i = -1; i <= 1; i++) {
        draw_dot(l, cx + i * (s / 3), cy + s / 3 + 3, 2, lv_color_hex(0xEAF6FF));
    }
}

static void icon_fog(lv_layer_t *l, int cx, int cy, int s, lv_color_t c)
{
    (void)c;
    for (int i = -1; i <= 1; i++) {
        draw_line(l, cx - s / 2, cy + i * (s / 4), cx + s / 2 - (i == 1 ? 4 : 0),
                  cy + i * (s / 4), 3, lv_color_hex(0xB9C4CF));
    }
}

static void icon_bolt(lv_layer_t *l, int cx, int cy, int s, lv_color_t c)
{
    icon_cloud(l, cx, cy - 4, s, c);
    draw_line(l, cx + 2, cy + 4, cx - 3, cy + 12, 2, lv_color_hex(0xFFD24A));
    draw_line(l, cx - 3, cy + 12, cx + 3, cy + 12, 2, lv_color_hex(0xFFD24A));
    draw_line(l, cx + 3, cy + 12, cx - 2, cy + 20, 2, lv_color_hex(0xFFD24A));
}

static void icon_sunrise(lv_layer_t *l, int cx, int cy, int s, lv_color_t c)
{
    (void)c;
    const int r = s / 3;
    draw_dot(l, cx, cy, r, lv_color_hex(0xFFC24A));
    draw_rect(l, cx - r - 1, cy + 1, cx + r + 1, cy + r, 0, bg_at(cy + r));
    draw_line(l, cx - s / 2, cy + 1, cx + s / 2, cy + 1, 2, lv_color_hex(0xDCE6F0));
    for (int i = -1; i <= 1; i++) {
        const float a = (float)i * 35.0f * PI / 180.0f - PI / 2;
        draw_line(l, cx + cosf(a) * (r + 4), cy + sinf(a) * (r + 4),
                  cx + cosf(a) * (r + 8), cy + sinf(a) * (r + 8), 2, lv_color_hex(0xFFA726));
    }
}

static void icon_for(lv_layer_t *l, int code, bool is_day, int cx, int cy, int s, lv_color_t c)
{
    if (code == 0) {
        if (is_day) {
            icon_sun(l, cx, cy, s, c);
        } else {
            icon_moon(l, cx, cy, s, c);
        }
    } else if (code <= 2) {
        if (is_day) {
            icon_sun(l, cx - s / 3, cy - s / 3, s * 2 / 3, c);
        } else {
            icon_moon(l, cx - s / 3, cy - s / 3, s * 2 / 3, c);
        }
        icon_cloud(l, cx + s / 6, cy + s / 6, s * 3 / 4, c);
    } else if (code == 3) {
        icon_cloud(l, cx, cy, s, c);
    } else if (code >= 45 && code <= 48) {
        icon_fog(l, cx, cy, s, c);
    } else if (code >= 51 && code <= 67) {
        icon_rain(l, cx, cy, s, c);
    } else if (code >= 71 && code <= 77) {
        icon_snow(l, cx, cy, s, c);
    } else if (code >= 80 && code <= 82) {
        icon_rain(l, cx, cy, s, c);
    } else if (code >= 85 && code <= 86) {
        icon_snow(l, cx, cy, s, c);
    } else if (code >= 95) {
        icon_bolt(l, cx, cy, s, c);
    } else {
        icon_cloud(l, cx, cy, s, c);
    }
}

/* ---------------- 页面 ---------------- */

static void redraw(void)
{
    if (!s_canvas || !s_visible) {
        return;
    }

    lv_layer_t layer;
    lv_canvas_init_layer(s_canvas, &layer);

    lv_draw_rect_dsc_t bg;
    lv_draw_rect_dsc_init(&bg);
    bg.bg_color = lv_color_hex(0x6C93B8);
    bg.bg_opa = LV_OPA_COVER;
    bg.bg_grad.dir = LV_GRAD_DIR_VER;
    bg.bg_grad.stops_count = 2;
    bg.bg_grad.stops[0].color = lv_color_hex(0x6C93B8);
    bg.bg_grad.stops[0].opa = LV_OPA_COVER;
    bg.bg_grad.stops[0].frac = 0;
    bg.bg_grad.stops[1].color = lv_color_hex(0x3E6185);
    bg.bg_grad.stops[1].opa = LV_OPA_COVER;
    bg.bg_grad.stops[1].frac = 255;
    lv_area_t full = { 0, 0, AW - 1, AH - 1 };
    lv_draw_rect(&layer, &bg, &full);

    /* 半透明圆角卡片，模拟例图的玻璃质感 */
    lv_draw_rect_dsc_t card;
    lv_draw_rect_dsc_init(&card);
    card.radius = 30;
    card.bg_color = lv_color_white();
    card.bg_opa = LV_OPA_10;
    card.border_width = 0;
    lv_area_t card_area = { 12, 8, AW - 13, AH - 9 };
    lv_draw_rect(&layer, &card, &card_area);

    const lv_color_t white = lv_color_white();
    const lv_color_t grey = lv_color_hex(0xCFDEEE);
    const lv_color_t dim = lv_color_hex(0x9FB6CB);

    /* 左上角：地区 */
    const char *city = weather_city();
    if (!city || !city[0]) {
        city = "AUTO";
    }
    draw_text(&layer, city, &font_cjk36, white, 44, 16, 420, LV_TEXT_ALIGN_LEFT);
    int cw = text_width(city, &font_cjk36);
    if (cw > 380) {
        cw = 380;
    }
    const int ax = 44 + cw + 18;
    const int ay = 40;
    draw_line(&layer, ax, ay, ax + 10, ay - 10, 3, grey);
    draw_line(&layer, ax + 4, ay - 10, ax + 10, ay - 10, 3, grey);
    draw_line(&layer, ax + 10, ay - 6, ax + 10, ay - 10, 3, grey);

    /* 当前温度 */
    if (weather_has_data()) {
        snprintf(s_big_tmp, sizeof(s_big_tmp), "%d°", weather_temp_now());
    } else {
        snprintf(s_big_tmp, sizeof(s_big_tmp), "--°");
    }
    draw_text(&layer, s_big_tmp, &font_temp, white, 36, 80, 340, LV_TEXT_ALIGN_LEFT);

    /* 右上角：天气图标 + 文字 */
    icon_for(&layer, weather_code_now(), true, 704, 72, 26, white);
    draw_text(&layer, weather_condition(), &font_cjk26, white, 704 - 100, 102, 200, LV_TEXT_ALIGN_CENTER);

    /* 最高 / 最低 */
    int hi = 0, lo = 0;
    if (weather_high_low(&hi, &lo)) {
        snprintf(s_hl_tmp, sizeof(s_hl_tmp), "%d°", hi);
        snprintf(s_lo_tmp, sizeof(s_lo_tmp), "%d°", lo);
        const int w1 = text_width("最高", &font_cjk20) + 8 + text_width(s_hl_tmp, &font_cjk26);
        const int w2 = text_width("最低", &font_cjk20) + 8 + text_width(s_lo_tmp, &font_cjk26);
        int x = 748 - (w1 + 26 + w2);
        draw_text(&layer, "最高", &font_cjk20, grey, x, 146, 80, LV_TEXT_ALIGN_LEFT);
        x += text_width("最高", &font_cjk20) + 8;
        draw_text(&layer, s_hl_tmp, &font_cjk26, white, x, 142, 60, LV_TEXT_ALIGN_LEFT);
        x += text_width(s_hl_tmp, &font_cjk26) + 26;
        draw_text(&layer, "最低", &font_cjk20, grey, x, 146, 80, LV_TEXT_ALIGN_LEFT);
        x += text_width("最低", &font_cjk20) + 8;
        draw_text(&layer, s_lo_tmp, &font_cjk26, white, x, 142, 60, LV_TEXT_ALIGN_LEFT);
    }

    /* 逐小时预报；日出按时间顺序插入，并在日出后不再显示 */
    weather_hour_t hrs[8];
    const int n = weather_hourly_next(hrs, 8);
    const char *ss = weather_sunrise();
    int sh = -1, sm = 0;
    if (ss[0]) {
        sscanf(ss, "%d:%d", &sh, &sm);
    }
    struct tm nowtm;
    const bool now_ok = (clock_get(&nowtm) == 0);
    const int sun_minutes = (sh >= 0) ? (sh * 60 + sm) : -1;
    bool sun_pending = (sun_minutes >= 0 && now_ok &&
                        sun_minutes > nowtm.tm_hour * 60 + nowtm.tm_min);

    int slot = 0;
    for (int i = 0; i < n && slot < 6; i++) {
        if (sun_pending && sun_minutes <= hrs[i].hour * 60) {
            const int cx = 106 + slot * 118;
            draw_text(&layer, ss, &font_cjk26, white, cx - 55, 206, 110, LV_TEXT_ALIGN_CENTER);
            icon_sunrise(&layer, cx, 266, 26, white);
            snprintf(s_hour_val[slot], sizeof(s_hour_val[slot]), "%d°", hrs[i].temp);
            draw_text(&layer, s_hour_val[slot], &font_cjk, white, cx - 55, 300, 110, LV_TEXT_ALIGN_CENTER);
            slot++;
            sun_pending = false;
        }
        if (slot >= 6) {
            break;
        }
        const int cx = 106 + slot * 118;
        snprintf(s_hour_lbl[slot], sizeof(s_hour_lbl[slot]), "%d时", hrs[i].hour);
        draw_text(&layer, s_hour_lbl[slot], &font_cjk26, white, cx - 55, 206, 110, LV_TEXT_ALIGN_CENTER);
        icon_for(&layer, hrs[i].code, hrs[i].is_day, cx, 266, 26, white);
        snprintf(s_hour_val[slot], sizeof(s_hour_val[slot]), "%d°", hrs[i].temp);
        draw_text(&layer, s_hour_val[slot], &font_cjk, white, cx - 55, 300, 110, LV_TEXT_ALIGN_CENTER);
        slot++;
    }
    if (slot == 0) {
        draw_text(&layer, "--", &font_cjk26, dim, 51, 266, 110, LV_TEXT_ALIGN_CENTER);
    }

    /* 上次更新时间（离线缓存时也能看出数据新旧） */
    const uint32_t up = weather_updated_at();
    if (up > 1700000000UL) {
        time_t t = (time_t)up;
        struct tm uptm;
        localtime_r(&t, &uptm);
        snprintf(s_upd_tmp, sizeof(s_upd_tmp), "updated %02d:%02d", uptm.tm_hour, uptm.tm_min);
        draw_text(&layer, s_upd_tmp, &font_cjk20, dim, 0, 430, AW, LV_TEXT_ALIGN_CENTER);
    }

    lv_canvas_finish_layer(s_canvas, &layer);
}

static void timer_cb(lv_timer_t *t)
{
    (void)t;
    redraw();
}

static void click_cb(lv_event_t *e)
{
    (void)e;
    ui_weather_show(false);
    ui_clock_show_info();
}

void ui_weather_init(void)
{
    if (s_canvas) {
        return;
    }
    s_buf = heap_caps_malloc(AW * AH * 2, MALLOC_CAP_SPIRAM);
    if (!s_buf) {
        return;
    }

    lvgl_port_lock(0);
    lv_obj_t *scr = lv_screen_active();
    s_canvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(s_canvas, s_buf, AW, AH, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas, 0, 0);
    lv_obj_set_size(s_canvas, AW, AH);
    lv_obj_set_style_bg_color(s_canvas, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_canvas, 0, 0);
    lv_obj_set_style_pad_all(s_canvas, 0, 0);
    lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_canvas, click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
    lv_timer_create(timer_cb, 30000, NULL);
    lvgl_port_unlock();
}

void ui_weather_show(bool on)
{
    if (!s_canvas) {
        return;
    }
    lvgl_port_lock(0);
    s_visible = on;
    if (on) {
        lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_canvas);
        redraw();
    } else {
        lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
    }
    lvgl_port_unlock();
}

bool ui_weather_visible(void)
{
    return s_visible;
}

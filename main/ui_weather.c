#include "ui_weather.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "clock.h"
#include "esp_heap_caps.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "lvgl.h"
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
static char s_upd_tmp[32];
static char s_grid[3][8];
static char s_ptemp[24][8];
static char s_phour[24][8];
static char s_dnum[7][8];
static char s_dwday[7][24];
static char s_dhi[7][8];
static char s_dlo[7][8];
static char s_hl_hi[8];
static char s_hl_lo[8];

#define C_WHITE lv_color_hex(0xF2F6FA)
#define C_GREY  lv_color_hex(0xCFDEEE)
#define C_DIM   lv_color_hex(0xB9CBDD)
#define C_GRID  lv_color_hex(0x7C9AB4)
#define C_ACC   lv_color_hex(0xE0662E)

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

static void icon_sun(lv_layer_t *l, int cx, int cy, int s)
{
    draw_dot(l, cx, cy, s / 2, lv_color_hex(0xFFC24A));
    for (int i = 0; i < 8; i++) {
        const float a = (float)i * 45.0f * PI / 180.0f;
        draw_line(l, cx + cosf(a) * (s / 2 + 2), cy + sinf(a) * (s / 2 + 2),
                  cx + cosf(a) * (s / 2 + 6), cy + sinf(a) * (s / 2 + 6), 2,
                  lv_color_hex(0xFFA726));
    }
}

static void icon_cloud(lv_layer_t *l, int cx, int cy, int s)
{
    const lv_color_t c = lv_color_hex(0xDCE6F0);
    const int r = s / 3;
    draw_dot(l, cx - r, cy + r / 3, r, c);
    draw_dot(l, cx + r / 4, cy - r / 2, r * 4 / 3, c);
    draw_dot(l, cx + r * 4 / 3, cy + r / 4, r * 9 / 10, c);
    draw_rect(l, cx - r * 2, cy + r / 6, cx + r * 3 / 2, cy + r, r / 2, c);
}

static void icon_rain(lv_layer_t *l, int cx, int cy, int s)
{
    icon_cloud(l, cx, cy - 2, s);
    for (int i = -1; i <= 1; i++) {
        const int x = cx + i * (s / 3);
        draw_line(l, x, cy + s / 3, x - 2, cy + s / 3 + 6, 2, lv_color_hex(0x6FB6F2));
    }
}

static void icon_snow(lv_layer_t *l, int cx, int cy, int s)
{
    icon_cloud(l, cx, cy - 2, s);
    for (int i = -1; i <= 1; i++) {
        draw_dot(l, cx + i * (s / 3), cy + s / 3 + 2, 2, lv_color_hex(0xEAF6FF));
    }
}

static void icon_fog(lv_layer_t *l, int cx, int cy, int s)
{
    for (int i = -1; i <= 1; i++) {
        draw_line(l, cx - s / 2, cy + i * (s / 4), cx + s / 2, cy + i * (s / 4), 2,
                  lv_color_hex(0xB9C4CF));
    }
}

static void icon_bolt(lv_layer_t *l, int cx, int cy, int s)
{
    icon_cloud(l, cx, cy - 3, s);
    draw_line(l, cx + 2, cy + 3, cx - 2, cy + 9, 2, lv_color_hex(0xFFD24A));
    draw_line(l, cx - 2, cy + 9, cx + 2, cy + 9, 2, lv_color_hex(0xFFD24A));
    draw_line(l, cx + 2, cy + 9, cx - 1, cy + 15, 2, lv_color_hex(0xFFD24A));
}

static void draw_icon(lv_layer_t *l, int code, int cx, int cy, int s)
{
    if (code == 0) {
        icon_sun(l, cx, cy, s);
    } else if (code <= 2) {
        icon_sun(l, cx - s / 3, cy - s / 3, s * 2 / 3);
        icon_cloud(l, cx + s / 6, cy + s / 6, s * 3 / 4);
    } else if (code == 3) {
        icon_cloud(l, cx, cy, s);
    } else if (code >= 45 && code <= 48) {
        icon_fog(l, cx, cy, s);
    } else if (code >= 51 && code <= 67) {
        icon_rain(l, cx, cy, s);
    } else if (code >= 71 && code <= 77) {
        icon_snow(l, cx, cy, s);
    } else if (code >= 80 && code <= 82) {
        icon_rain(l, cx, cy, s);
    } else if (code >= 85 && code <= 86) {
        icon_snow(l, cx, cy, s);
    } else if (code >= 95) {
        icon_bolt(l, cx, cy, s);
    } else {
        icon_cloud(l, cx, cy, s);
    }
}

static int weekday_of(int y, int m, int d)
{
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) {
        y -= 1;
    }
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static const char *wd_cn(int w)
{
    static const char *n[7] = { "周日", "周一", "周二", "周三", "周四", "周五", "周六" };
    return n[(w + 7) % 7];
}

static void redraw(void)
{
    if (!s_canvas || !s_visible) {
        return;
    }

    lv_layer_t layer;
    lv_canvas_init_layer(s_canvas, &layer);

    /* 背景渐变 */
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

    lv_draw_rect_dsc_t card;
    lv_draw_rect_dsc_init(&card);
    card.radius = 30;
    card.bg_color = lv_color_white();
    card.bg_opa = LV_OPA_10;
    card.border_width = 0;
    lv_area_t card_area = { 12, 8, AW - 13, AH - 9 };
    lv_draw_rect(&layer, &card, &card_area);

    /* 左上：地区 */
    const char *city = weather_city();
    if (!city || !city[0]) {
        city = "AUTO";
    }
    draw_text(&layer, city, &font_cjk36, C_WHITE, 40, 2, 460, LV_TEXT_ALIGN_LEFT);
    int cw = text_width(city, &font_cjk36);
    if (cw > 360) {
        cw = 360;
    }
    const int ax = 40 + cw + 14;
    const int ay = 26;
    draw_line(&layer, ax, ay, ax + 9, ay - 9, 2, C_GREY);
    draw_line(&layer, ax + 4, ay - 9, ax + 9, ay - 9, 2, C_GREY);
    draw_line(&layer, ax + 9, ay - 5, ax + 9, ay - 9, 2, C_GREY);

    /* 大温度 */
    if (weather_has_data()) {
        snprintf(s_big_tmp, sizeof(s_big_tmp), "%d°", weather_temp_now());
    } else {
        snprintf(s_big_tmp, sizeof(s_big_tmp), "--°");
    }
    draw_text(&layer, s_big_tmp, &font_temp, C_WHITE, 32, 54, 320, LV_TEXT_ALIGN_LEFT);

    /* 右上：天气图标 + 文字 */
    draw_icon(&layer, weather_code_now(), 704, 30, 22);
    draw_text(&layer, weather_condition(), &font_cjk20, C_WHITE, 624, 48, 160, LV_TEXT_ALIGN_CENTER);

    /* 最高 / 最低 */
    int hi = 0, lo = 0;
    if (weather_high_low(&hi, &lo)) {
        snprintf(s_hl_hi, sizeof(s_hl_hi), "%d°", hi);
        snprintf(s_hl_lo, sizeof(s_hl_lo), "%d°", lo);
        const int w1 = text_width("最高", &font_cjk20) + 6 + text_width(s_hl_hi, &font_cjk26);
        const int w2 = text_width("最低", &font_cjk20) + 6 + text_width(s_hl_lo, &font_cjk26);
        int x = 748 - (w1 + 22 + w2);
        draw_text(&layer, "最高", &font_cjk20, C_GREY, x, 76, 80, LV_TEXT_ALIGN_LEFT);
        x += text_width("最高", &font_cjk20) + 6;
        draw_text(&layer, s_hl_hi, &font_cjk26, C_WHITE, x, 72, 60, LV_TEXT_ALIGN_LEFT);
        x += text_width(s_hl_hi, &font_cjk26) + 22;
        draw_text(&layer, "最低", &font_cjk20, C_GREY, x, 76, 80, LV_TEXT_ALIGN_LEFT);
        x += text_width("最低", &font_cjk20) + 6;
        draw_text(&layer, s_hl_lo, &font_cjk26, C_WHITE, x, 72, 60, LV_TEXT_ALIGN_LEFT);
    }

    /* ---------- 24 小时曲线 ---------- */
    weather_hour_t hrs[24];
    const int n = weather_hourly_next(hrs, 24);
    if (n >= 2) {
        int tmin = hrs[0].temp, tmax = hrs[0].temp;
        for (int i = 1; i < n; i++) {
            if (hrs[i].temp < tmin) {
                tmin = hrs[i].temp;
            }
            if (hrs[i].temp > tmax) {
                tmax = hrs[i].temp;
            }
        }
        if (tmax == tmin) {
            tmax = tmin + 1;
        }

        const int X0 = 72, X1 = 752, Y0 = 138, Y1 = 220;
        for (int g = 0; g <= 2; g++) {
            const int t = tmax - (tmax - tmin) * g / 2;
            const int y = Y0 + (Y1 - Y0) * g / 2;
            snprintf(s_grid[g], sizeof(s_grid[g]), "%d°", t);
            draw_text(&layer, s_grid[g], &font_cjk20, C_GREY, 8, y - 11, 58, LV_TEXT_ALIGN_RIGHT);
        }

        int px[24], py[24];
        for (int i = 0; i < n; i++) {
            px[i] = X0 + (X1 - X0) * i / (n - 1);
            py[i] = Y1 - (hrs[i].temp - tmin) * (Y1 - Y0) / (tmax - tmin);
        }
        for (int i = 1; i < n; i++) {
            draw_line(&layer, px[i - 1], py[i - 1], px[i], py[i], 3, C_WHITE);
        }
        for (int i = 0; i < n; i++) {
            draw_dot(&layer, px[i], py[i], 3, C_WHITE);
            if (i > 0 && i % 3 == 0) {
                snprintf(s_ptemp[i], sizeof(s_ptemp[i]), "%d°", hrs[i].temp);
                draw_text(&layer, s_ptemp[i], &font_cjk20, C_WHITE, px[i] - 30, py[i] - 30, 60, LV_TEXT_ALIGN_CENTER);
                snprintf(s_phour[i], sizeof(s_phour[i]), "%d时", hrs[i].hour);
                draw_text(&layer, s_phour[i], &font_cjk26, C_DIM, px[i] - 34, 224, 68, LV_TEXT_ALIGN_CENTER);
            }
        }
    } else {
        draw_text(&layer, "--", &font_cjk20, C_DIM, 360, 180, 80, LV_TEXT_ALIGN_CENTER);
    }

    /* ---------- 未来 7 天 ---------- */
    weather_day_t days[7];
    const int nd = weather_daily(days, 7);
    for (int i = 0; i < nd; i++) {
        const int cx = 74 + i * 108;
        snprintf(s_dnum[i], sizeof(s_dnum[i]), "%d", days[i].day);
        draw_text(&layer, s_dnum[i], &font_cjk26, C_WHITE, cx - 50, 276, 100, LV_TEXT_ALIGN_CENTER);
        if (i == 0) {
            draw_rect(&layer, cx - 13, 305, cx + 13, 308, 2, C_ACC);
        }
        snprintf(s_dwday[i], sizeof(s_dwday[i]), "%s", wd_cn(weekday_of(days[i].year, days[i].month, days[i].day)));
        draw_text(&layer, s_dwday[i], &font_cjk20, C_DIM, cx - 50, 312, 100, LV_TEXT_ALIGN_CENTER);

        draw_icon(&layer, days[i].code, cx, 352, 22);

        snprintf(s_dhi[i], sizeof(s_dhi[i]), "%d°", days[i].hi);
        draw_text(&layer, s_dhi[i], &font_cjk26, C_WHITE, cx - 50, 370, 100, LV_TEXT_ALIGN_CENTER);
        snprintf(s_dlo[i], sizeof(s_dlo[i]), "%d°", days[i].lo);
        draw_text(&layer, s_dlo[i], &font_cjk20, C_DIM, cx - 50, 402, 100, LV_TEXT_ALIGN_CENTER);
    }

    /* 更新时间 */
    const uint32_t up = weather_updated_at();
    if (up > 1700000000UL) {
        time_t t = (time_t)up;
        struct tm tm;
        localtime_r(&t, &tm);
        snprintf(s_upd_tmp, sizeof(s_upd_tmp), "updated %02d:%02d", tm.tm_hour, tm.tm_min);
        draw_text(&layer, s_upd_tmp, &font_cjk20, C_DIM, 0, 442, AW, LV_TEXT_ALIGN_CENTER);
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
    lv_timer_create(timer_cb, 60000, NULL);
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

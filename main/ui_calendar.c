#include "ui_calendar.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "clock.h"
#include "esp_heap_caps.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "lunar.h"
#include "ui_clock.h"
#include "ui_weather.h"

#define AW 800
#define AH 480

LV_FONT_DECLARE(font_num36);
LV_FONT_DECLARE(font_cjk);
LV_FONT_DECLARE(font_cjk26);
LV_FONT_DECLARE(font_cjk20);
LV_FONT_DECLARE(font_cjk36);

static lv_obj_t *s_canvas;
static uint16_t *s_buf;
static bool s_visible;
static char s_day_txt[42][16];
static char s_lunar_txt[42][24];
static char s_title_txt[32];

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

static const char *s_month_cn[13] = { "", "一月", "二月", "三月", "四月", "五月", "六月",
                                      "七月", "八月", "九月", "十月", "十一月", "十二月" };

/* 取农历的“日”部分：如 十月十八 -> 十八 */
static void lunar_day_text(int y, int m, int d, char *out, size_t n)
{
    lunar_date_t ld;
    out[0] = '\0';
    if (!lunar_from_solar(y, m, d, &ld)) {
        return;
    }
    char full[40];
    lunar_format(&ld, full, sizeof(full));
    const char *p = strstr(full, "月");
    snprintf(out, n, "%s", p ? p + 3 : full);
}

static int weekday_of(int y, int m, int d)
{
    /* 0=周日 */
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) {
        y -= 1;
    }
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static int days_in_month(int y, int m)
{
    static const int dm[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) {
        return 29;
    }
    return dm[m - 1];
}

static void redraw(void)
{
    if (!s_canvas || !s_visible) {
        return;
    }

    lv_layer_t layer;
    lv_canvas_init_layer(s_canvas, &layer);

    lv_draw_rect_dsc_t bg;
    lv_draw_rect_dsc_init(&bg);
    bg.bg_color = lv_color_black();
    bg.bg_opa = LV_OPA_COVER;
    lv_area_t full = { 0, 0, AW - 1, AH - 1 };
    lv_draw_rect(&layer, &bg, &full);

    struct tm tm;
    const bool ok = (clock_get(&tm) == 0);
    int year = 2026, mon = 1, mday = 1;
    if (ok) {
        year = tm.tm_year + 1900;
        mon = tm.tm_mon + 1;
        mday = tm.tm_mday;
    }

    lv_color_t red = lv_color_hex(0xF03A2E);

    /* 顶部居中：2026年11月 */
    snprintf(s_title_txt, sizeof(s_title_txt), "%d年%d月", year, mon);
    draw_text(&layer, s_title_txt, &font_cjk36, red, 0, 4, AW, LV_TEXT_ALIGN_CENTER);

    /* 星期表头 */
    const char *wd[7] = { "日", "一", "二", "三", "四", "五", "六" };
    for (int i = 0; i < 7; i++) {
        const int cx = 57 + i * 114;
        draw_text(&layer, wd[i], &font_cjk26, lv_color_hex(0x9AA0A6),
                  cx - 57, 54, 114, LV_TEXT_ALIGN_CENTER);
    }

    /* 日期网格：6 行 × 7 列 */
    const int first_wd = weekday_of(year, mon, 1);
    const int dim = days_in_month(year, mon);
    const int prev_dim = days_in_month(mon == 1 ? year - 1 : year, mon == 1 ? 12 : mon - 1);

    for (int cell = 0; cell < 42; cell++) {
        const int r = cell / 7, c = cell % 7;
        const int cx = 57 + c * 114;
        const int cy = 100 + r * 64 + 26;

        int day;
        bool in_month = true;
        if (cell < first_wd) {
            day = prev_dim - first_wd + 1 + cell;
            in_month = false;
        } else if (cell >= first_wd + dim) {
            day = cell - first_wd - dim + 1;
            in_month = false;
        } else {
            day = cell - first_wd + 1;
        }

        const bool today = (in_month && day == mday);

        if (today) {
            draw_dot(&layer, cx, cy, 21, red);
        }

        lv_color_t col;
        if (today) {
            col = lv_color_white();
        } else if (!in_month) {
            col = lv_color_hex(0x4A4E53);
        } else if (c == 0) {
            col = lv_color_hex(0x8A8F94);
        } else {
            col = lv_color_hex(0xF2F4F6);
        }

        snprintf(s_day_txt[cell], sizeof(s_day_txt[cell]), "%d", day);
        draw_text(&layer, s_day_txt[cell], &font_num36, col,
                  cx - 50, cy - 20, 100, LV_TEXT_ALIGN_CENTER);

        if (in_month) {
            lunar_day_text(year, mon, day, s_lunar_txt[cell], sizeof(s_lunar_txt[cell]));
            if (s_lunar_txt[cell][0]) {
                draw_text(&layer, s_lunar_txt[cell], &font_cjk20,
                          today ? lv_color_white() : lv_color_hex(0x7C838A),
                          cx - 50, cy + 9, 100, LV_TEXT_ALIGN_CENTER);
            }
        }
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
    ui_calendar_show(false);
    ui_weather_show(true);
}

void ui_calendar_init(void)
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

void ui_calendar_show(bool on)
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

bool ui_calendar_visible(void)
{
    return s_visible;
}

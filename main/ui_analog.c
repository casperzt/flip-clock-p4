#include "ui_analog.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "clock.h"
#include "esp_heap_caps.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "lunar.h"
#include "lvgl.h"
#include "ui_calendar.h"
#include "ui_clock.h"

#define AW 800
#define AH 480
#define CX 400
#define CY 240
#define PI 3.14159265f

LV_FONT_DECLARE(font_num84);
LV_FONT_DECLARE(font_cjk);

static lv_obj_t *s_canvas;
static uint16_t *s_buf;
static bool s_visible;

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

static void draw_text(lv_layer_t *l, const char *txt, const lv_font_t *f, lv_color_t c, int x, int y, int w)
{
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.text = txt;
    d.font = f;
    d.color = c;
    d.align = LV_TEXT_ALIGN_CENTER;
    lv_area_t a = { x - w / 2, y, x + w / 2, y + f->line_height };
    lv_draw_label(l, &d, &a);
}

static void draw_hand(lv_layer_t *l, float ang, float neck, float len, int neck_w, int body_w, lv_color_t c)
{
    const float sn = sinf(ang), cs = cosf(ang);
    const float nx = CX + sn * neck, ny = CY - cs * neck;
    draw_line(l, CX, CY, nx, ny, neck_w, c);
    draw_line(l, nx, ny, CX + sn * len, CY - cs * len, body_w, c);
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

    /* 长方形表盘：刻度沿矩形边框向内，15 分位置最长 */
    const float W2 = 368.0f, H2 = 214.0f;
    for (int i = 0; i < 60; i++) {
        const float th = (float)i * 6.0f * PI / 180.0f;
        const float dx = sinf(th), dy = -cosf(th);
        const float tx = fabsf(dx) > 0.0001f ? W2 / fabsf(dx) : 1e9f;
        const float ty = fabsf(dy) > 0.0001f ? H2 / fabsf(dy) : 1e9f;
        const float r = tx < ty ? tx : ty;
        const float px = CX + dx * r, py = CY + dy * r;

        const bool major = (i % 5 == 0);
        const bool card = (i % 15 == 0);
        lv_color_t col = card ? lv_color_hex(0xA8AEB5) : (major ? lv_color_hex(0x7D838A) : lv_color_hex(0x3E4348));
        const int w = card ? 5 : (major ? 4 : 2);

        if (card) {
            /* 只管外段：数字内侧不再画线 */
            draw_line(&layer, px, py,
                      CX + dx * (r - 46), CY + dy * (r - 46), w, col);
        } else {
            const float len = major ? 34.0f : 20.0f;
            draw_line(&layer, px, py,
                      CX + dx * (r - len), CY + dy * (r - len), w, col);
        }
    }

    struct tm tm;
    const bool ok = (clock_get(&tm) == 0);
    const int hh = ok ? tm.tm_hour : 0;
    const int mm = ok ? tm.tm_min : 0;
    const int ss = ok ? tm.tm_sec : 0;

    /* 四个方位数字，落在刻度缺口里 */
    const char *nums[4] = { "12", "3", "6", "9" };
    for (int k = 0; k < 4; k++) {
        const float th = (float)k * 90.0f * PI / 180.0f;
        const float dx = sinf(th), dy = -cosf(th);
        const float tx = fabsf(dx) > 0.0001f ? W2 / fabsf(dx) : 1e9f;
        const float ty = fabsf(dy) > 0.0001f ? H2 / fabsf(dy) : 1e9f;
        const float r = (tx < ty ? tx : ty) - 95.0f;
        const int x = CX + (int)(dx * r);
        const int y = CY + (int)(dy * r);
        draw_text(&layer, nums[k], &font_num84, lv_color_white(),
                  x, y - font_num84.line_height / 2, 220);
    }

    /* 右侧日期 */
    if (ok) {
        char day[8];
        snprintf(day, sizeof(day), "%d", tm.tm_mday);
        draw_text(&layer, day, &font_cjk, lv_color_white(), CX + 128, CY - 16, 90);
        draw_text(&layer, weekday_cn(tm.tm_wday), &font_cjk, lv_color_hex(0xE0662E), CX + 186, CY - 16, 110);
    }

    /* 指针 */
    const float ah = ((hh % 12) + mm / 60.0f) * 30.0f * PI / 180.0f;
    const float am = (mm + ss / 60.0f) * 6.0f * PI / 180.0f;
    const float as = ss * 6.0f * PI / 180.0f;

    /* 时针：细颈 + 粗圆头 */
    draw_hand(&layer, ah, 22, 100, 6, 16, lv_color_white());
    /* 分针：细颈 + 细长圆头 */
    draw_hand(&layer, am, 20, 132, 5, 10, lv_color_white());
    /* 秒针：细橙线，两侧都伸出 */
    draw_line(&layer, CX - 26 * sinf(as), CY + 26 * cosf(as),
              CX + 148 * sinf(as), CY - 148 * cosf(as), 3, lv_color_hex(0xD9641F));

    /* 轴心：白环 + 橙环 + 白点 */
    draw_dot(&layer, CX, CY, 14, lv_color_white());
    draw_dot(&layer, CX, CY, 9, lv_color_black());
    draw_dot(&layer, CX, CY, 8, lv_color_hex(0xD9641F));
    draw_dot(&layer, CX, CY, 3, lv_color_white());

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
    ui_analog_show(false);
    ui_calendar_show(true);
}

void ui_analog_init(void)
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
    lv_timer_create(timer_cb, 1000, NULL);
    lvgl_port_unlock();
}

void ui_analog_show(bool on)
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

bool ui_analog_visible(void)
{
    return s_visible;
}

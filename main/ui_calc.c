#include "ui_calc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "ui_clock.h"

LV_FONT_DECLARE(font_calc);
LV_FONT_DECLARE(font_sqrt);
LV_FONT_DECLARE(font_cjk20);
LV_FONT_DECLARE(font_cjk26);

static lv_obj_t *s_page;
static lv_obj_t *s_disp;
static lv_obj_t *s_title;

static double s_mem;
static bool s_mem_set;

static double s_acc;
static char s_cur[24];
static int s_curlen;
static char s_pend;      /* '+', '-', '*', '/' */
static char s_lastop;
static double s_lastv;
static bool s_entering;
static bool s_err;

static void update(void)
{
    if (s_title) {
        lv_label_set_text(s_title, s_mem_set ? "CALCULATOR   [M]" : "CALCULATOR");
    }
    char buf[40];
    if (s_err) {
        snprintf(buf, sizeof(buf), "Error");
    } else if (s_entering) {
        snprintf(buf, sizeof(buf), "%s", s_curlen ? s_cur : "0");
    } else {
        snprintf(buf, sizeof(buf), "%.10g", s_acc);
    }
    lv_label_set_text(s_disp, buf);
}

static void reset_all(void)
{
    s_acc = 0;
    s_cur[0] = '\0';
    s_curlen = 0;
    s_pend = 0;
    s_lastop = 0;
    s_lastv = 0;
    s_entering = false;
    s_err = false;
    update();
}

static double current_value(void)
{
    return s_entering ? atof(s_cur) : s_acc;
}

static void apply(char op, double v)
{
    switch (op) {
    case '+': s_acc += v; break;
    case '-': s_acc -= v; break;
    case '*': s_acc *= v; break;
    case '/':
        if (v == 0) {
            s_err = true;
        } else {
            s_acc /= v;
        }
        break;
    default: s_acc = v; break;
    }
}

static void on_digit(int d)
{
    if (s_err) {
        reset_all();
    }
    if (!s_entering) {
        s_curlen = 0;
        s_cur[0] = '\0';
        s_entering = true;
    }
    if (s_curlen < 12) {
        s_cur[s_curlen++] = (char)('0' + d);
        s_cur[s_curlen] = '\0';
    }
    update();
}

static void on_dot(void)
{
    if (s_err) {
        reset_all();
    }
    if (!s_entering) {
        s_curlen = 0;
        s_entering = true;
        s_cur[s_curlen++] = '0';
        s_cur[s_curlen] = '\0';
    }
    if (!strchr(s_cur, '.')) {
        if (s_curlen < 12) {
            s_cur[s_curlen++] = '.';
            s_cur[s_curlen] = '\0';
        }
    }
    update();
}

static void on_op(char op)
{
    if (s_err) {
        return;
    }
    const double v = current_value();
    if (s_pend) {
        apply(s_pend, v);
        if (s_err) {
            update();
            return;
        }
    } else {
        s_acc = v;
    }
    s_pend = op;
    s_entering = false;
    update();
}

static void on_eq(void)
{
    if (s_err) {
        return;
    }
    if (s_pend) {
        const double v = current_value();
        apply(s_pend, v);
        s_lastop = s_pend;
        s_lastv = v;
        s_pend = 0;
        s_entering = false;
        update();
    } else if (s_lastop) {
        /* 连按等号：重复上一次运算 */
        apply(s_lastop, s_lastv);
        update();
    }
}

static void on_unary(int kind)
{
    double v = current_value();
    switch (kind) {
    case 8: v = sqrt(v); break;           /* 根号 */
    case 9: v = v * v; break;             /* 平方 */
    case 10: v = (v == 0) ? NAN : 1.0 / v; break; /* 倒数 */
    default: break;
    }
    if (isnan(v) || isinf(v)) {
        s_err = true;
        update();
        return;
    }
    if (s_entering) {
        snprintf(s_cur, sizeof(s_cur), "%.10g", v);
        s_curlen = (int)strlen(s_cur);
    } else {
        s_acc = v;
    }
    update();
}

static void on_pct(void)
{
    if (s_err) {
        return;
    }
    double v = current_value() / 100.0;
    if (s_entering) {
        snprintf(s_cur, sizeof(s_cur), "%.10g", v);
        s_curlen = (int)strlen(s_cur);
        s_entering = true;
    } else {
        s_acc = v;
    }
    update();
}

static void on_sign(void)
{
    if (s_err) {
        return;
    }
    if (s_entering) {
        if (s_cur[0] == '-') {
            memmove(s_cur, s_cur + 1, strlen(s_cur));
            s_curlen--;
        } else if (s_curlen < 12) {
            memmove(s_cur + 1, s_cur, strlen(s_cur) + 1);
            s_cur[0] = '-';
            s_curlen++;
        }
    } else {
        s_acc = -s_acc;
    }
    update();
}

static void on_mem(int kind)
{
    switch (kind) {
    case 11: /* MC */
        s_mem = 0;
        s_mem_set = false;
        break;
    case 12: /* MR */
        if (s_mem_set) {
            snprintf(s_cur, sizeof(s_cur), "%.10g", s_mem);
            s_curlen = (int)strlen(s_cur);
            s_entering = true;
            s_err = false;
        }
        break;
    case 13: /* M+ */
        s_mem += current_value();
        s_mem_set = true;
        break;
    case 14: /* M- */
        s_mem -= current_value();
        s_mem_set = true;
        break;
    default:
        break;
    }
    update();
}

static void on_back(void)
{
    if (s_err) {
        reset_all();
        return;
    }
    if (s_entering && s_curlen > 0) {
        s_cur[--s_curlen] = '\0';
        if (s_curlen == 0) {
            s_entering = false;
        }
    }
    update();
}

static void key_cb(lv_event_t *e)
{
    const intptr_t k = (intptr_t)lv_event_get_user_data(e);
    const char ch = (char)(k & 0xFF);
    const int kind = (int)((k >> 8) & 0xFF);

    switch (kind) {
    case 0: /* 数字 */
        on_digit(ch - '0');
        break;
    case 1: /* 小数点 */
        on_dot();
        break;
    case 2: /* 运算符 */
        on_op(ch);
        break;
    case 3: /* 等号 */
        on_eq();
        break;
    case 4: /* C */
        reset_all();
        break;
    case 5: /* 退格 */
        on_back();
        break;
    case 6: /* % */
        on_pct();
        break;
    case 7: /* ± */
        on_sign();
        break;
    case 8: /* 一元运算 */
        on_unary((int)ch);
        break;
    case 9: /* 存储 */
        on_mem((int)ch);
        break;
    default:
        break;
    }
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_calc_show(false);
    ui_clock_show_info();
}

static lv_obj_t *mk_key(lv_obj_t *parent, int x, int y, int w, int h, const char *txt,
                        int kind, char ch, uint32_t bg)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, key_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)(((intptr_t)kind << 8) | (unsigned char)ch));
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    const lv_font_t *font = &font_cjk26;
    if (kind == 8 && ch == 8) {
        font = &font_sqrt;
    } else if (kind == 5) {
        font = &lv_font_montserrat_20;
    }
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xE6EDF3), 0);
    lv_obj_center(l);
    return b;
}

void ui_calc_init(void)
{
    if (s_page) {
        return;
    }

    lvgl_port_lock(0);
    lv_obj_t *scr = lv_screen_active();
    s_page = lv_obj_create(scr);
    lv_obj_set_pos(s_page, 0, 0);
    lv_obj_set_size(s_page, 800, 480);
    lv_obj_set_style_bg_color(s_page, lv_color_hex(0x0A0B0C), 0);
    lv_obj_set_style_bg_opa(s_page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_page, 0, 0);
    lv_obj_set_style_pad_all(s_page, 0, 0);
    lv_obj_clear_flag(s_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_page, LV_OBJ_FLAG_CLICKABLE);

    s_title = lv_label_create(s_page);
    lv_obj_set_style_text_font(s_title, &font_cjk20, 0);
    lv_obj_set_style_text_color(s_title, lv_color_hex(0x8AB4F8), 0);
    lv_label_set_text(s_title, "CALCULATOR");
    lv_obj_set_pos(s_title, 40, 16);

    s_disp = lv_label_create(s_page);
    lv_obj_set_style_text_font(s_disp, &font_calc, 0);
    lv_obj_set_style_text_color(s_disp, lv_color_white(), 0);
    lv_label_set_text(s_disp, "0");
    lv_obj_set_pos(s_disp, 30, 40);
    lv_obj_set_width(s_disp, 690);
    lv_obj_set_style_text_align(s_disp, LV_TEXT_ALIGN_RIGHT, 0);

    /* 按键区：4 列 × 6 行 */
    const int X0 = 40, Y0 = 166, W = 170, H = 38, GX = 16, GY = 4;
    const int xc[4] = { X0, X0 + W + GX, X0 + 2 * (W + GX), X0 + 3 * (W + GX) };
    const int yr[7] = { Y0, Y0 + H + GY, Y0 + 2 * (H + GY), Y0 + 3 * (H + GY),
                        Y0 + 4 * (H + GY), Y0 + 5 * (H + GY), Y0 + 6 * (H + GY) };

    const uint32_t C_NUM = 0x16191E;
    const uint32_t C_FN = 0x22262D;
    const uint32_t C_OP = 0x8A4B1E;
    const uint32_t C_EQ = 0x2E7D4F;

    mk_key(s_page, xc[0], yr[0], W, H, "C", 4, 'C', C_FN);
    mk_key(s_page, xc[1], yr[0], W, H, LV_SYMBOL_BACKSPACE, 5, 0, C_FN);
    {
        lv_obj_t *bs = mk_key(s_page, xc[2], yr[0], W, H, "", 8, 8, C_FN);
        lv_obj_t *ic = lv_obj_create(bs);
        lv_obj_set_size(ic, 52, 30);
        lv_obj_set_style_bg_opa(ic, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(ic, 0, 0);
        lv_obj_set_style_pad_all(ic, 0, 0);
        lv_obj_center(ic);
        static lv_point_precise_t rad[] = { { 2, 16 }, { 7, 24 }, { 16, 1 }, { 50, 1 } };
        lv_obj_t *ln = lv_line_create(ic);
        lv_line_set_points(ln, rad, 4);
        lv_obj_set_style_line_width(ln, 2, 0);
        lv_obj_set_style_line_color(ln, lv_color_hex(0xE6EDF3), 0);
        lv_obj_t *l2 = lv_label_create(ic);
        lv_label_set_text(l2, "2");
        lv_obj_set_style_text_font(l2, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(l2, lv_color_hex(0xE6EDF3), 0);
        lv_obj_set_pos(l2, 0, -4);
        lv_obj_t *lx = lv_label_create(ic);
        lv_label_set_text(lx, "x");
        lv_obj_set_style_text_font(lx, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lx, lv_color_hex(0xE6EDF3), 0);
        lv_obj_set_pos(lx, 28, 6);
    }
    mk_key(s_page, xc[3], yr[0], W, H, "/", 2, '/', C_OP);

    mk_key(s_page, xc[0], yr[1], W, H, "7", 0, '7', C_NUM);
    mk_key(s_page, xc[1], yr[1], W, H, "8", 0, '8', C_NUM);
    mk_key(s_page, xc[2], yr[1], W, H, "9", 0, '9', C_NUM);
    mk_key(s_page, xc[3], yr[1], W, H, "x", 2, '*', C_OP);

    mk_key(s_page, xc[0], yr[2], W, H, "4", 0, '4', C_NUM);
    mk_key(s_page, xc[1], yr[2], W, H, "5", 0, '5', C_NUM);
    mk_key(s_page, xc[2], yr[2], W, H, "6", 0, '6', C_NUM);
    mk_key(s_page, xc[3], yr[2], W, H, "-", 2, '-', C_OP);

    mk_key(s_page, xc[0], yr[3], W, H, "1", 0, '1', C_NUM);
    mk_key(s_page, xc[1], yr[3], W, H, "2", 0, '2', C_NUM);
    mk_key(s_page, xc[2], yr[3], W, H, "3", 0, '3', C_NUM);
    mk_key(s_page, xc[3], yr[3], W, H, "+", 2, '+', C_OP);

    mk_key(s_page, xc[0], yr[4], W * 2 + GX, H, "0", 0, '0', C_NUM);
    mk_key(s_page, xc[2], yr[4], W, H, ".", 1, 0, C_NUM);
    mk_key(s_page, xc[3], yr[4], W, H, "=", 3, 0, C_EQ);

    mk_key(s_page, xc[0], yr[5], W, H, "%", 6, 0, C_FN);
    mk_key(s_page, xc[1], yr[5], W, H, "+/-", 7, 0, C_FN);
    mk_key(s_page, xc[2], yr[5], W, H, "x2", 8, 9, C_FN);
    mk_key(s_page, xc[3], yr[5], W, H, "1/x", 8, 10, C_FN);

    mk_key(s_page, xc[0], yr[6], W, H, "MC", 9, 11, C_FN);
    mk_key(s_page, xc[1], yr[6], W, H, "MR", 9, 12, C_FN);
    mk_key(s_page, xc[2], yr[6], W, H, "M+", 9, 13, C_FN);
    mk_key(s_page, xc[3], yr[6], W, H, "M-", 9, 14, C_FN);

    lv_obj_t *back = lv_button_create(s_page);
    lv_obj_set_pos(back, 752, 10);
    lv_obj_set_size(back, 38, 38);
    lv_obj_set_style_radius(back, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x2F3A45), 0);
    lv_obj_set_style_border_width(back, 0, 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_16, 0);
    lv_obj_center(bl);

    lv_obj_add_flag(s_page, LV_OBJ_FLAG_HIDDEN);
    reset_all();
    lvgl_port_unlock();
}

void ui_calc_show(bool on)
{
    if (!s_page) {
        return;
    }
    lvgl_port_lock(0);
    if (on) {
        reset_all();
        lv_obj_clear_flag(s_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_page);
    } else {
        lv_obj_add_flag(s_page, LV_OBJ_FLAG_HIDDEN);
    }
    lvgl_port_unlock();
}

bool ui_calc_visible(void)
{
    return s_page && !lv_obj_has_flag(s_page, LV_OBJ_FLAG_HIDDEN);
}

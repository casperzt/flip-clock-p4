#include "ui_game.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <stdio.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_cache.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "driver/ppa.h"
#include "board_display.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <event.h>
#include <log.h>
#include <nes.h>
#include <nofconfig.h>
#include <nofrendo.h>

#include "ui_clock.h"
#include "wifi_time.h"

#define AW 800
#define AH 480
#define GW 256
#define GH 240
#define GX ((AW - GW * 2) / 2)   /* 144 */
#define PANEL_W 480
#define PANEL_H 800
#define ROM_PATH "/spiffs/game.nes"

LV_FONT_DECLARE(font_cjk20);

static const char *TAG = "game";

static lv_obj_t *s_canvas;
static uint16_t *s_buf;
static bool s_visible;
static lv_obj_t *s_hint;
static lv_obj_t *s_btns[10];
static lv_obj_t *s_exit;

static ppa_client_handle_t s_ppa;
static uint16_t *s_rgb;

static volatile int s_input;
static volatile int s_rapid;   /* bit0 = A 连发, bit1 = B 连发 */
static volatile bool s_running;
static volatile bool s_stop;
static TaskHandle_t s_task;

#define BIT_SELECT 0x01
#define BIT_START  0x02
#define BIT_UP     0x04
#define BIT_RIGHT  0x08
#define BIT_DOWN   0x10
#define BIT_LEFT   0x20
#define BIT_A      0x40
#define BIT_B      0x80

static bool rom_exists(void)
{
    struct stat st;
    return stat(ROM_PATH, &st) == 0 && st.st_size > 16;
}

static void emu_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "starting nofrendo: %s", ROM_PATH);
    config.filename = (char *)"na";
    nofrendo_set_video_cb(ui_game_blit);
    nofrendo_set_input_cb(ui_game_input);
    log_init();
    event_init();
    s_running = true;
    main_loop(ROM_PATH, system_nes);
    nofrendo_release();
    s_running = false;
    s_task = NULL;
    ESP_LOGI(TAG, "emulator stopped");
    vTaskDelete(NULL);
}

static void set_pause(bool on)
{
    nes_t *n = nes_getcontextptr();
    if (n) {
        n->pause = on;
    }
}

void ui_game_blit(const uint8_t *fb, const uint16_t *pal)
{
    if (!s_visible) {
        return;
    }

    /* 直接写面板帧缓冲：PPA 旋转 90° + 2x 放大 */
    if (!s_ppa) {
        ppa_client_config_t pcfg = { .oper_type = PPA_OPERATION_SRM };
        if (ppa_register_client(&pcfg, &s_ppa) != ESP_OK) {
            s_ppa = NULL;
        }
    }
    if (!s_rgb) {
        s_rgb = heap_caps_aligned_alloc(64, GW * GH * 2, MALLOC_CAP_SPIRAM);
    }
    void *fbv = NULL;
    if (!s_ppa || !s_rgb ||
        esp_lcd_dpi_panel_get_frame_buffer(board_display_panel(), 1, &fbv) != ESP_OK || !fbv) {
        return;
    }

    for (int i = 0; i < GW * GH; i++) {
        s_rgb[i] = pal[fb[i]];
    }
    esp_cache_msync(s_rgb, GW * GH * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

    ppa_srm_oper_config_t op = {
        .in = {
            .buffer = s_rgb,
            .pic_w = GW,
            .pic_h = GH,
            .block_w = GW,
            .block_h = GH,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = fbv,
            .buffer_size = PANEL_W * PANEL_H * 2,
            .pic_w = PANEL_W,
            .pic_h = PANEL_H,
            .block_offset_x = 0,
            .block_offset_y = GX,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_90,
        .scale_x = 2.0f,
        .scale_y = 2.0f,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    ppa_do_scale_rotate_mirror(s_ppa, &op);
}

int ui_game_input(void)
{
    static int tick;
    int v = s_input;
    tick++;
    /* 连发：每 4 帧翻转一次（约 7.5 次/秒） */
    if (s_rapid & 1) {
        if ((tick / 4) & 1) {
            v |= BIT_A;
        } else {
            v &= ~BIT_A;
        }
    }
    if (s_rapid & 2) {
        if ((tick / 4) & 1) {
            v |= BIT_B;
        } else {
            v &= ~BIT_B;
        }
    }
    return v;
}

/* ---------------- 触摸按键 ---------------- */

static void ctl_cb(lv_event_t *e)
{
    const int bit = (int)(intptr_t)lv_event_get_user_data(e);
    const lv_event_code_t code = lv_event_get_code(e);
    if (bit == 0) {
        return;
    }
    if (code == LV_EVENT_PRESSED) {
        s_input |= bit;
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        s_input &= ~bit;
    }
}

static void rapid_cb(lv_event_t *e)
{
    const int mask = (int)(intptr_t)lv_event_get_user_data(e);
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_rapid |= mask;
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        s_rapid &= ~mask;
    }
}

static lv_obj_t *mk_btn(lv_obj_t *parent, int x, int y, int w, int h, const char *txt,
                        int bit, const lv_font_t *font)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x15181D), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_70, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0x3A434D), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, ctl_cb, LV_EVENT_PRESSED, (void *)(intptr_t)bit);
    lv_obj_add_event_cb(b, ctl_cb, LV_EVENT_RELEASED, (void *)(intptr_t)bit);
    lv_obj_add_event_cb(b, ctl_cb, LV_EVENT_PRESS_LOST, (void *)(intptr_t)bit);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xE6EDF3), 0);
    lv_obj_center(l);
    return b;
}

static void exit_cb(lv_event_t *e)
{
    (void)e;
    ui_game_show(false);
    ui_clock_show_info();
}

static void canvas_click_cb(lv_event_t *e)
{
    (void)e;
}

static void make_ui(void)
{
    lv_obj_t *scr = lv_screen_active();

    s_buf = heap_caps_malloc(AW * AH * 2, MALLOC_CAP_SPIRAM);
    if (!s_buf) {
        ESP_LOGE(TAG, "canvas alloc failed");
        return;
    }
    memset(s_buf, 0, AW * AH * 2);

    s_canvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(s_canvas, s_buf, AW, AH, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas, 0, 0);
    lv_obj_set_size(s_canvas, AW, AH);
    lv_obj_set_style_bg_color(s_canvas, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_canvas, 0, 0);
    lv_obj_set_style_pad_all(s_canvas, 0, 0);
    lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_canvas, canvas_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);

    /* 左侧十字键（实体手柄布局） */
    s_btns[0] = mk_btn(scr, 42, 98, 60, 60, LV_SYMBOL_UP, BIT_UP, &lv_font_montserrat_20);
    s_btns[1] = mk_btn(scr, 0, 158, 68, 60, LV_SYMBOL_LEFT, BIT_LEFT, &lv_font_montserrat_20);
    s_btns[2] = mk_btn(scr, 76, 158, 68, 60, LV_SYMBOL_RIGHT, BIT_RIGHT, &lv_font_montserrat_20);
    s_btns[3] = mk_btn(scr, 42, 218, 60, 60, LV_SYMBOL_DOWN, BIT_DOWN, &lv_font_montserrat_20);

    /* 左下：SELECT / START */
    s_btns[4] = mk_btn(scr, 8, 320, 62, 34, "SELECT", BIT_SELECT, &lv_font_montserrat_14);
    s_btns[5] = mk_btn(scr, 76, 320, 62, 34, "START", BIT_START, &lv_font_montserrat_14);

    /* 右侧 2×2：上排连发 B/A，下排 B/A */
    s_btns[6] = mk_btn(scr, 664, 150, 60, 52, "B+", 0, &lv_font_montserrat_16);
    s_btns[7] = mk_btn(scr, 730, 150, 60, 52, "A+", 0, &lv_font_montserrat_16);
    lv_obj_add_event_cb(s_btns[6], rapid_cb, LV_EVENT_PRESSED, (void *)(intptr_t)2);
    lv_obj_add_event_cb(s_btns[6], rapid_cb, LV_EVENT_RELEASED, (void *)(intptr_t)2);
    lv_obj_add_event_cb(s_btns[6], rapid_cb, LV_EVENT_PRESS_LOST, (void *)(intptr_t)2);
    lv_obj_add_event_cb(s_btns[7], rapid_cb, LV_EVENT_PRESSED, (void *)(intptr_t)1);
    lv_obj_add_event_cb(s_btns[7], rapid_cb, LV_EVENT_RELEASED, (void *)(intptr_t)1);
    lv_obj_add_event_cb(s_btns[7], rapid_cb, LV_EVENT_PRESS_LOST, (void *)(intptr_t)1);
    lv_obj_set_style_bg_color(s_btns[6], lv_color_hex(0x3A2B1A), 0);
    lv_obj_set_style_bg_color(s_btns[7], lv_color_hex(0x3A2B1A), 0);

    s_btns[8] = mk_btn(scr, 664, 226, 60, 60, "B", BIT_B, &lv_font_montserrat_28);
    s_btns[9] = mk_btn(scr, 730, 226, 60, 60, "A", BIT_A, &lv_font_montserrat_28);

    /* 退出 */
    s_exit = mk_btn(scr, 752, 8, 40, 40, LV_SYMBOL_CLOSE, 0, &lv_font_montserrat_16);
    lv_obj_add_event_cb(s_exit, exit_cb, LV_EVENT_CLICKED, NULL);

    /* 没 ROM 时的提示 */
    s_hint = lv_label_create(scr);
    lv_obj_set_style_text_font(s_hint, &font_cjk20, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(0xCFDEEE), 0);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_hint, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < 10; i++) {
        lv_obj_add_flag(s_btns[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(s_exit, LV_OBJ_FLAG_HIDDEN);
}

void ui_game_init(void)
{
    if (s_canvas) {
        return;
    }
    lvgl_port_lock(0);
    make_ui();
    lvgl_port_unlock();
}

void ui_game_show(bool on)
{
    if (!s_canvas || on == s_visible) {
        return;
    }

    if (on) {
        const bool rom = rom_exists();
        if (rom && !s_running) {
            s_stop = false;
            s_input = 0;
            BaseType_t tc = xTaskCreatePinnedToCore(emu_task, "nes", 20480, NULL, 3, &s_task, 1);
            if (tc != pdPASS) {
                ESP_LOGE(TAG, "task create failed: %d", (int)tc);
            }
        } else if (rom) {
            set_pause(false);
        }

        lvgl_port_lock(0);
        s_visible = true;
        lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_canvas);
        for (int i = 0; i < 10; i++) {
            lv_obj_clear_flag(s_btns[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_btns[i]);
        }
        lv_obj_clear_flag(s_exit, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_exit);
        if (!rom) {
            char msg[128];
            snprintf(msg, sizeof(msg), "NO ROM\nUpload: http://%s/game", wifi_time_ip());
            lv_label_set_text(s_hint, msg);
            lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_hint);
        } else {
            lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        }
        lvgl_port_unlock();
    } else {
        /* 退出时只暂停，保留模拟器实例，避免重复初始化导致不稳定 */
        set_pause(true);
        lvgl_port_lock(0);
        s_visible = false;
        lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < 10; i++) {
            lv_obj_add_flag(s_btns[i], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_flag(s_exit, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(lv_screen_active());
        lvgl_port_unlock();
    }
}

bool ui_game_visible(void)
{
    return s_visible;
}

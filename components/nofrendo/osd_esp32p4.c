/* OSD layer for nofrendo on ESP32-P4 (canvas video, touch input, no audio) */

#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include <noftypes.h>
#include <nofconfig.h>
#include <event.h>
#include <log.h>
#include <nes.h>
#include <nesinput.h>
#include <nofrendo.h>
#include <osd.h>
#include <vid_drv.h>
#include <bitmap.h>

/* 由 main 注入的视频/输入回调（避免组件反向依赖 main） */
static void (*s_video_cb)(const uint8_t *fb, const uint16_t *pal);
static int (*s_input_cb)(void);

void nofrendo_set_video_cb(void (*cb)(const uint8_t *fb, const uint16_t *pal))
{
    s_video_cb = cb;
}

void nofrendo_set_input_cb(int (*cb)(void))
{
    s_input_cb = cb;
}

#define NES_W 256
#define NES_H 240

static const char *TAG = "nes-osd";

static uint8_t s_fb[NES_W * NES_H];
static uint16_t s_pal[256];
static bitmap_t *s_bmp;
static esp_timer_handle_t s_timer;
static void (*s_tick)(void);
static void (*s_sound)(void *buffer, int length);

static int drv_init(int width, int height)
{
    (void)width;
    (void)height;
    return 0;
}

static void drv_shutdown(void)
{
}

static int drv_set_mode(int width, int height)
{
    (void)width;
    (void)height;
    return 0;
}

static void drv_set_palette(rgb_t *pal)
{
    for (int i = 0; i < 256; i++) {
        s_pal[i] = (uint16_t)(((pal[i].r & 0xF8) << 8) |
                              ((pal[i].g & 0xFC) << 3) |
                              (pal[i].b >> 3));
    }
}

static void drv_clear(uint8 color)
{
    memset(s_fb, color, sizeof(s_fb));
}

static bitmap_t *drv_lock_write(void)
{
    s_bmp = bmp_createhw(s_fb, NES_W, NES_H, NES_W);
    if (!s_bmp) {
        ESP_LOGE(TAG, "bitmap alloc failed");
    }
    return s_bmp;
}

static void drv_free_write(int num_dirties, rect_t *dirty_rects)
{
    (void)num_dirties;
    (void)dirty_rects;
    bmp_destroy(&s_bmp);
}

static void drv_blit(bitmap_t *primary, int num_dirties, rect_t *dirty_rects)
{
    (void)num_dirties;
    (void)dirty_rects;
    if (s_video_cb && primary && primary->data) {
        s_video_cb(primary->data, s_pal);
    }
}

static viddriver_t s_driver = {
    "ESP32-P4 canvas",
    drv_init,
    drv_shutdown,
    drv_set_mode,
    drv_set_palette,
    drv_clear,
    drv_lock_write,
    drv_free_write,
    drv_blit,
    false,
};

void osd_getvideoinfo(vidinfo_t *info)
{
    info->default_width = NES_W;
    info->default_height = NES_H;
    info->driver = &s_driver;
}

void osd_getsoundinfo(sndinfo_t *info)
{
    info->sample_rate = 22050;
    info->bps = 16;
}

int osd_init(void)
{
    return 0;
}

void osd_shutdown(void)
{
}

int osd_main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    return 0;
}

static void timer_cb(void *arg)
{
    (void)arg;
    if (s_tick) {
        s_tick();
    }
}

int osd_installtimer(int frequency, void *func, int funcsize, void *counter, int countersize)
{
    (void)funcsize;
    (void)counter;
    (void)countersize;
    s_tick = (void (*)(void))func;
    if (!s_timer) {
        const esp_timer_create_args_t args = {
            .callback = timer_cb,
            .name = "nes_tick",
        };
        if (esp_timer_create(&args, &s_timer) != ESP_OK) {
            return -1;
        }
    } else {
        esp_timer_stop(s_timer);
    }
    if (frequency <= 0) {
        frequency = 60;
    }
    ESP_LOGI(TAG, "frame timer %d Hz", frequency);
    return esp_timer_start_periodic(s_timer, 1000000 / frequency) == ESP_OK ? 0 : -1;
}

void osd_setsound(void (*playfunc)(void *buffer, int length))
{
    s_sound = playfunc;
}

char *osd_getromdata(void)
{
    FILE *f = fopen("/spiffs/game.nes", "rb");
    if (!f) {
        ESP_LOGE(TAG, "rom not found");
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 16 || sz > 4 * 1024 * 1024) {
        fclose(f);
        ESP_LOGE(TAG, "bad rom size %ld", sz);
        return NULL;
    }
    char *buf = malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    ESP_LOGI(TAG, "rom loaded: %ld bytes", sz);
    return buf;
}

void osd_fullname(char *fullname, const char *shortname)
{
    snprintf(fullname, PATH_MAX, "%s", shortname);
}

char *osd_newextension(char *string, char *ext)
{
    (void)ext;
    return string;
}

int osd_makesnapname(char *filename, int len)
{
    (void)filename;
    (void)len;
    return -1;
}

void osd_getmouse(int *x, int *y, int *button)
{
    (void)x;
    (void)y;
    (void)button;
}

void osd_idle(void)
{
    vTaskDelay(1);
}

void osd_getinput(void)
{
    static const int evs[8] = {
        event_joypad1_select,
        event_joypad1_start,
        event_joypad1_up,
        event_joypad1_right,
        event_joypad1_down,
        event_joypad1_left,
        event_joypad1_a,
        event_joypad1_b,
    };
    static int old = 0;
    const int b = s_input_cb ? s_input_cb() : 0;
    int chg = b ^ old;
    old = b;

    for (int i = 0; i < 8; i++) {
        if (chg & (1 << i)) {
            event_t ev = event_get(evs[i]);
            if (ev) {
                ev((b & (1 << i)) ? INP_STATE_MAKE : INP_STATE_BREAK);
            }
        }
    }
}

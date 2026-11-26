#include "screen_stream.h"

#include <stdio.h>
#include <string.h>

#include "board_display.h"
#include "driver/jpeg_encode.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/base64.h"
#include "esp_lvgl_port.h"

#define LCD_W 480
#define LCD_H 800
#define JPG_CAP (128 * 1024)
#define SW 320
#define SH 533

static const char *TAG = "stream";

static volatile int s_tap_x;
static volatile int s_tap_y;
static volatile bool s_tap_pending;
static lv_indev_t *s_tap_indev;

static void tap_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    if (s_tap_pending) {
        data->point.x = s_tap_x;
        data->point.y = s_tap_y;
        data->state = LV_INDEV_STATE_PRESSED;
        s_tap_pending = false;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static jpeg_encoder_handle_t s_jpeg;
static uint8_t *s_jpg;
static uint8_t *s_copy;
static uint16_t *s_small;
static SemaphoreHandle_t s_lock;
static bool s_started;
static volatile bool s_mirror;
static volatile bool s_enabled;

static const char s_page[] =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Flip Clock</title><style>"
    "body{margin:0;background:#0a0b0c;color:#888;font:14px system-ui;"
    "display:flex;flex-direction:column;align-items:center;justify-content:center;min-height:100vh}"
    ".wrap{width:min(96vw,960px);aspect-ratio:5/3;position:relative;overflow:hidden;"
    "background:#000;border-radius:10px}"
    "img{position:absolute;left:50%;top:50%;width:60%;height:auto;"
    "transform:translate(-50%,-50%) rotate(90deg);image-rendering:auto}"
    "button{margin-top:12px;padding:8px 16px;border-radius:8px;border:0;"
    "background:#243040;color:#cfe;font-size:14px}"
    "</style></head><body>"
    "<div class=wrap><img id=v src='/snap'></div>"
    "<button onclick=rot()>rotate</button>"
    "<script>let r=90;function rot(){r=(r+90)%360;"
    "document.getElementById('v').style.transform='translate(-50%,-50%) rotate('+r+'deg)';}"
    "setInterval(()=>{document.getElementById('v').src='/snap?t='+Date.now();},600);"
    "document.querySelector('.wrap').addEventListener('click',e=>{"
    "const b=document.getElementById('v').getBoundingClientRect();"
    "const dx=(e.clientX-b.left)/b.width, dy=(e.clientY-b.top)/b.height;"
    "let x,y;"
    "if(r===90){x=dy*480;y=800-dx*800;}"
    "else if(r===270){x=480-dy*480;y=dx*800;}"
    "else if(r===180){x=480-dx*480;y=800-dy*800;}"
    "else{x=dx*480;y=dy*800;}"
    "fetch('/tap?x='+Math.round(x)+'&y='+Math.round(y));});"
    "</script></body></html>";

static esp_err_t tap_get(httpd_req_t *req)
{
    if (!s_enabled) {
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "stream off");
    }
    char q[64];
    char v[16];
    int x = -1, y = -1;
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        if (httpd_query_key_value(q, "x", v, sizeof(v)) == ESP_OK) {
            x = atoi(v);
        }
        if (httpd_query_key_value(q, "y", v, sizeof(v)) == ESP_OK) {
            y = atoi(v);
        }
    }
    if (x < 0 || y < 0 || x >= LCD_W || y >= LCD_H) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad coords");
    }
    s_tap_x = x;
    s_tap_y = y;
    s_tap_pending = true;
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t stream_ctl_get(httpd_req_t *req)
{
    char q[32];
    char v[8];
    bool on = s_enabled;
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "on", v, sizeof(v)) == ESP_OK) {
        on = atoi(v) != 0;
        screen_stream_set_enabled(on);
    }
    return httpd_resp_sendstr(req, on ? "on" : "off");
}

static const char s_game_page[] =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>NES ROM</title><style>"
    "body{margin:0;background:#0a0b0c;color:#cfe;font:15px system-ui;"
    "display:flex;flex-direction:column;align-items:center;justify-content:center;min-height:100vh}"
    "input,button{margin:10px;font-size:16px}button{padding:10px 22px;border-radius:10px;"
    "border:0;background:#2B6CB0;color:#fff}</style></head><body>"
    "<h3>NES ROM upload</h3>"
    "<input type=file id=f accept='.nes'>"
    "<button onclick=up()>Upload</button><div id=s></div>"
    "<script>"
    "async function up(){const f=document.getElementById('f').files[0];"
    "const s=document.getElementById('s');if(!f){s.textContent='choose a .nes file';return;}"
    "s.textContent='uploading '+Math.round(f.size/1024)+' KB...';"
    "const r=await fetch('/rom',{method:'PUT',body:f});"
    "s.textContent=r.ok?'done: '+f.name+' — 回到设备点 GAME':'failed: '+r.status;}"
    "</script></body></html>";

static esp_err_t game_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, s_game_page, sizeof(s_game_page) - 1);
}

static esp_err_t rom_put(httpd_req_t *req)
{
    if (req->content_len <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no body");
    }
    ESP_LOGI(TAG, "rom upload start: %d bytes", req->content_len);
    FILE *f = fopen("/spiffs/game.nes", "wb");
    if (!f) {
        ESP_LOGE(TAG, "rom open failed");
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "open failed");
    }
    char *buf = malloc(4096);
    if (!buf) {
        fclose(f);
        return httpd_resp_send_500(req);
    }
    int left = req->content_len;
    while (left > 0) {
        int chunk = left < 4096 ? left : 4096;
        int r = httpd_req_recv(req, buf, chunk);
        if (r <= 0) {
            free(buf);
            fclose(f);
            ESP_LOGE(TAG, "rom recv failed: %d left=%d", r, left);
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv failed");
        }
        if (fwrite(buf, 1, r, f) != (size_t)r) {
            free(buf);
            fclose(f);
            ESP_LOGE(TAG, "rom write failed");
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write failed");
        }
        left -= r;
    }
    free(buf);
    fclose(f);
    ESP_LOGI(TAG, "rom uploaded: %d bytes", req->content_len);
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t root_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, s_page, sizeof(s_page) - 1);
}

static esp_err_t snap_get(httpd_req_t *req)
{
    if (!s_enabled) {
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "stream off");
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(3000)) != pdTRUE) {
        return httpd_resp_send_500(req);
    }

    esp_err_t err = ESP_FAIL;
    void *fb = NULL;
    if (board_display_panel() &&
        esp_lcd_dpi_panel_get_frame_buffer(board_display_panel(), 1, &fb) == ESP_OK && fb) {
        /* 先下采样到 240x400，再编码（避免直接碰帧缓冲，也大幅提速） */
        const uint16_t *src = (const uint16_t *)fb;
        for (int y = 0; y < SH; y++) {
            const uint16_t *row = src + (size_t)(y * 3 / 2) * LCD_W;
            uint16_t *dst = s_small + (size_t)y * SW;
            for (int x = 0; x < SW; x++) {
                dst[x] = row[x * 3 / 2];
            }
        }
        memcpy(s_copy, s_small, SW * SH * 2);

        jpeg_encode_cfg_t cfg = {
            .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
            .sub_sample = JPEG_DOWN_SAMPLING_YUV420,
            .image_quality = 70,
            .width = SW,
            .height = SH,
        };
        uint32_t out_size = 0;
        err = jpeg_encoder_process(s_jpeg, &cfg, s_copy, SW * SH * 2, s_jpg, JPG_CAP, &out_size);
        if (err == ESP_OK) {
            httpd_resp_set_type(req, "image/jpeg");
            httpd_resp_set_hdr(req, "Cache-Control", "no-store");
            err = httpd_resp_send(req, (const char *)s_jpg, out_size);
        }
    }

    xSemaphoreGive(s_lock);
    /* 注意：send 失败后绝不能再往同一个已断开的连接写响应，否则会崩 */
    return err;
}

static void start_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(300));
    screen_stream_start();
    s_enabled = true;
    vTaskDelete(NULL);
}

void screen_stream_set_enabled(bool on)
{
    if (on) {
        if (!s_started) {
            xTaskCreate(start_task, "streamstart", 6144, NULL, 3, NULL);
        } else {
            s_enabled = true;
        }
    } else {
        s_enabled = false;
    }
    ESP_LOGI(TAG, "stream %s", on ? "ON" : "OFF");
}

bool screen_stream_enabled(void)
{
    return s_enabled;
}

void screen_stream_start(void)
{
    if (s_started) {
        ESP_LOGI(TAG, "already running");
        return;
    }
    ESP_LOGI(TAG, "step1: heap int=%u psram=%u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    jpeg_encode_engine_cfg_t eng = { .timeout_ms = 300 };
    ESP_LOGI(TAG, "step2: jpeg_new_encoder_engine...");
    esp_err_t jerr = jpeg_new_encoder_engine(&eng, &s_jpeg);
    ESP_LOGI(TAG, "step2 done: %s", esp_err_to_name(jerr));
    if (jerr != ESP_OK) {
        return;
    }

    ESP_LOGI(TAG, "step3: alloc buffers");

    jpeg_encode_memory_alloc_cfg_t in_cfg = { .buffer_direction = JPEG_ENC_ALLOC_INPUT_BUFFER };
    jpeg_encode_memory_alloc_cfg_t out_cfg = { .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER };
    size_t in_size = 0, out_size_alloc = 0;
    s_copy = jpeg_alloc_encoder_mem(SW * SH * 2, &in_cfg, &in_size);
    s_small = heap_caps_malloc(SW * SH * 2, MALLOC_CAP_SPIRAM);
    s_jpg = jpeg_alloc_encoder_mem(JPG_CAP, &out_cfg, &out_size_alloc);
    ESP_LOGI(TAG, "buffers: in=%p/%u out=%p/%u", s_copy, (unsigned)in_size, s_jpg, (unsigned)out_size_alloc);
    s_lock = xSemaphoreCreateMutex();
    if (!s_jpg || !s_copy || !s_lock) {
        ESP_LOGE(TAG, "no memory");
        return;
    }

    ESP_LOGI(TAG, "step4: httpd_start...");
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 6;
    httpd_handle_t srv = NULL;
    if (httpd_start(&srv, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd start failed");
        return;
    }
    ESP_LOGI(TAG, "step4 done: server up");
    httpd_uri_t uri_root = { .uri = "/", .method = HTTP_GET, .handler = root_get };
    httpd_uri_t uri_snap = { .uri = "/snap", .method = HTTP_GET, .handler = snap_get };
    httpd_uri_t uri_tap = { .uri = "/tap", .method = HTTP_GET, .handler = tap_get };
    httpd_uri_t uri_stream = { .uri = "/stream", .method = HTTP_GET, .handler = stream_ctl_get };
    httpd_register_uri_handler(srv, &uri_root);
    httpd_register_uri_handler(srv, &uri_snap);
    httpd_register_uri_handler(srv, &uri_tap);
    httpd_register_uri_handler(srv, &uri_stream);
    httpd_uri_t uri_game = { .uri = "/game", .method = HTTP_GET, .handler = game_get };
    httpd_uri_t uri_rom = { .uri = "/rom", .method = HTTP_PUT, .handler = rom_put };
    httpd_register_uri_handler(srv, &uri_game);
    httpd_register_uri_handler(srv, &uri_rom);

    lvgl_port_lock(0);
    s_tap_indev = lv_indev_create();
    lv_indev_set_type(s_tap_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_tap_indev, tap_read_cb);
    lv_indev_set_disp(s_tap_indev, lv_obj_get_display(lv_screen_active()));
    lvgl_port_unlock();
    s_started = true;
    ESP_LOGI(TAG, "screen stream ready: http://<device-ip>/");
}

/* ---------------- USB 串口镜像 ---------------- */
static void ensure_tap_indev(void)
{
    if (s_tap_indev) {
        return;
    }
    lvgl_port_lock(0);
    s_tap_indev = lv_indev_create();
    lv_indev_set_type(s_tap_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_tap_indev, tap_read_cb);
    lv_indev_set_disp(s_tap_indev, lv_obj_get_display(lv_screen_active()));
    lvgl_port_unlock();
}

void screen_tap(int x, int y)
{
    if (x < 0 || y < 0 || x >= LCD_W || y >= LCD_H) {
        return;
    }
    s_tap_x = x;
    s_tap_y = y;
    s_tap_pending = true;
}

static void mirror_task(void *arg)
{
    (void)arg;
    jpeg_encode_memory_alloc_cfg_t in_cfg = { .buffer_direction = JPEG_ENC_ALLOC_INPUT_BUFFER };
    jpeg_encode_memory_alloc_cfg_t out_cfg = { .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER };
    size_t in_sz = 0, out_sz = 0;
    uint8_t *small = jpeg_alloc_encoder_mem(SW * SH * 2, &in_cfg, &in_sz);
    uint8_t *jpg = jpeg_alloc_encoder_mem(64 * 1024, &out_cfg, &out_sz);
    if (!small || !jpg) {
        s_mirror = false;
        vTaskDelete(NULL);
        return;
    }

    while (s_mirror) {
        void *fb = NULL;
        if (board_display_panel() &&
            esp_lcd_dpi_panel_get_frame_buffer(board_display_panel(), 1, &fb) == ESP_OK && fb) {
            const uint16_t *src = (const uint16_t *)fb;
            uint16_t *dst = (uint16_t *)small;
            for (int y = 0; y < SH; y++) {
                const uint16_t *row = src + (size_t)(y * 3 / 2) * LCD_W;
                for (int x = 0; x < SW; x++) {
                    dst[y * SW + x] = row[x * 3 / 2];
                }
            }
            jpeg_encode_cfg_t cfg = {
                .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
                .sub_sample = JPEG_DOWN_SAMPLING_YUV420,
                .image_quality = 65,
                .width = SW,
                .height = SH,
            };
            uint32_t out = 0;
            if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) == pdTRUE) {
                if (s_jpeg && jpeg_encoder_process(s_jpeg, &cfg, small, SW * SH * 2, jpg, 64 * 1024, &out) == ESP_OK) {
                    size_t blen = 0;
                    static char *b64;
                    if (!b64) {
                        b64 = heap_caps_malloc(96 * 1024, MALLOC_CAP_SPIRAM);
                    }
                    if (b64 && mbedtls_base64_encode((unsigned char *)b64, 96 * 1024, &blen, jpg, out) == 0) {
                        printf("#IMG %u\n", (unsigned)blen);
                        for (size_t i = 0; i < blen; i += 76) {
                            size_t n = blen - i > 76 ? 76 : blen - i;
                            fwrite(b64 + i, 1, n, stdout);
                            fputc('\n', stdout);
                        }
                        printf("#END\n");
                        fflush(stdout);
                    }
                }
                xSemaphoreGive(s_lock);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    heap_caps_free(small);
    heap_caps_free(jpg);

    vTaskDelete(NULL);
}

void screen_mirror_set(bool on)
{
    if (on) {
        if (!s_lock) {
            s_lock = xSemaphoreCreateMutex();
        }
        if (!s_jpeg) {
            jpeg_encode_engine_cfg_t eng = { .timeout_ms = 300 };
            if (jpeg_new_encoder_engine(&eng, &s_jpeg) != ESP_OK) {
                ESP_LOGE(TAG, "jpeg init failed");
                return;
            }
        }
        ensure_tap_indev();
        if (!s_mirror) {
            s_mirror = true;
            xTaskCreate(mirror_task, "mirror", 8192, NULL, 4, NULL);
        }
        ESP_LOGI(TAG, "usb mirror ON");
    } else {
        s_mirror = false;
        ESP_LOGI(TAG, "usb mirror OFF");
    }
}

#include "board_touch.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_i2c.h"
#include "esp_check.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

#define LCD_H_RES 480
#define LCD_V_RES 800
#define PIN_TOUCH_RST 3
#define PIN_TOUCH_INT 21

static const char *TAG = "touch";

esp_err_t touch_init(lv_display_t *disp)
{
    /* 官方 BSP：先给 GT911 一个复位脉冲（GPIO3），否则触摸不工作 */
    gpio_config_t rst = {
        .pin_bit_mask = 1ULL << PIN_TOUCH_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&rst);
    gpio_set_level(PIN_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(60));

    uint8_t addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
    if (i2c_master_probe(board_i2c_get(), addr, 100) != ESP_OK) {
        addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
        ESP_LOGW(TAG, "0x5D no ack, trying 0x%02X", addr);
    }
    ESP_LOGI(TAG, "GT911 at 0x%02X", addr);

    esp_lcd_panel_io_handle_t io = NULL;
    const esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr = addr,
        .scl_speed_hz = 400000,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 16,
        .lcd_param_bits = 8,
        .flags.disable_control_phase = 1,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(board_i2c_get(), &io_cfg, &io), TAG, "io");

    esp_lcd_touch_io_gt911_config_t gt_cfg = {
        .dev_addr = addr,
    };
    esp_lcd_touch_config_t cfg = {
        .x_max = LCD_H_RES - 1,
        .y_max = LCD_V_RES - 1,
        .rst_gpio_num = PIN_TOUCH_RST,
        .int_gpio_num = GPIO_NUM_NC,  /* 中断脚不可靠，强制 LVGL 用轮询模式 */
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
        .driver_data = &gt_cfg,
    };

    esp_lcd_touch_handle_t tp = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(io, &cfg, &tp), TAG, "gt911");

    const lvgl_port_touch_cfg_t tcfg = {
        .disp = disp,
        .handle = tp,
    };
    if (!lvgl_port_add_touch(&tcfg)) {
        ESP_LOGE(TAG, "lvgl add touch failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "GT911 ready");
    return ESP_OK;
}

static i2c_master_dev_handle_t s_gt_dev;

esp_err_t gt911_raw_read(uint8_t *status, uint16_t *x, uint16_t *y)
{
    if (!s_gt_dev) {
        i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = 0x5D,
            .scl_speed_hz = 400000,
        };
        if (i2c_master_bus_add_device(board_i2c_get(), &cfg, &s_gt_dev) != ESP_OK) {
            return ESP_FAIL;
        }
    }
    uint8_t reg[2] = { 0x81, 0x4E };
    uint8_t st = 0;
    esp_err_t err = i2c_master_transmit_receive(s_gt_dev, reg, 2, &st, 1, 100);
    if (err != ESP_OK) {
        return err;
    }
    *status = st;
    if (st & 0x80) {
        uint8_t r2[2] = { 0x81, 0x50 };
        uint8_t buf[4] = { 0 };
        if (i2c_master_transmit_receive(s_gt_dev, r2, 2, buf, 4, 100) == ESP_OK) {
            *x = (uint16_t)(buf[0] | (buf[1] << 8));
            *y = (uint16_t)(buf[2] | (buf[3] << 8));
        }
        uint8_t clr[3] = { 0x81, 0x4E, 0x00 };
        i2c_master_transmit(s_gt_dev, clr, 3, 100);
    }
    return ESP_OK;
}

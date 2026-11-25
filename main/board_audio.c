#include "board_audio.h"

#include "board_i2c.h"

#include <inttypes.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "es8311_codec.h"

#define PIN_I2C_SDA   7
#define PIN_I2C_SCL   8
#define PIN_I2S_MCLK  13
#define PIN_I2S_BCLK  12
#define PIN_I2S_LRCK  10
#define PIN_I2S_DOUT  9
#define PIN_PA_EN     11

static const char *TAG = "audio";

static i2c_master_bus_handle_t s_i2c_bus;
static i2s_chan_handle_t s_tx;
static const audio_codec_data_if_t *s_data_if;
static esp_codec_dev_handle_t s_dev;
static uint32_t s_rate;

esp_err_t audio_init(void)
{
    if (s_dev) {
        return ESP_OK;
    }

    /* 共用 board_i2c 的总线 */
    s_i2c_bus = board_i2c_get();
    if (!s_i2c_bus) {
        ESP_LOGE(TAG, "i2c bus not ready");
        return ESP_ERR_INVALID_STATE;
    }

    /* I2S TX */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx, NULL), TAG, "i2s chan");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = PIN_I2S_MCLK,
            .bclk = PIN_I2S_BCLK,
            .ws = PIN_I2S_LRCK,
            .dout = PIN_I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = { 0 },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std_cfg), TAG, "i2s std");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "i2s enable");

    /* codec interfaces */
    audio_codec_i2s_cfg_t i2s_data_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = NULL,
        .tx_handle = s_tx,
    };
    s_data_if = audio_codec_new_i2s_data(&i2s_data_cfg);
    if (!s_data_if) {
        ESP_LOGE(TAG, "i2s data if failed");
        return ESP_FAIL;
    }

    audio_codec_i2c_cfg_t i2c_ctrl_cfg = {
        .port = I2C_NUM_0,
        .addr = ES8311_CODEC_DEFAULT_ADDR,  /* 0x30 = 7bit 0x18 << 1 */
        .bus_handle = s_i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_ctrl_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    if (!ctrl_if || !gpio_if) {
        ESP_LOGE(TAG, "codec ctrl/gpio if failed");
        return ESP_FAIL;
    }

    es8311_codec_cfg_t es_cfg = {
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .pa_pin = PIN_PA_EN,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .hw_gain = {
            .pa_voltage = 5.0,
            .codec_dac_voltage = 3.3,
        },
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es_cfg);
    if (!codec_if) {
        ESP_LOGE(TAG, "es8311 create failed");
        return ESP_FAIL;
    }

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = codec_if,
        .data_if = s_data_if,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    s_dev = esp_codec_dev_new(&dev_cfg);
    if (!s_dev) {
        ESP_LOGE(TAG, "codec dev create failed");
        return ESP_FAIL;
    }

    /* 功放使能脚先拉高，避免首帧被吃掉 */
    gpio_config_t pa = {
        .pin_bit_mask = 1ULL << PIN_PA_EN,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&pa);
    gpio_set_level(PIN_PA_EN, 1);

    ESP_LOGI(TAG, "audio hw ready (ES8311 @0x18, PA=GPIO%d)", PIN_PA_EN);
    return ESP_OK;
}

esp_err_t audio_open(uint32_t sample_rate, uint8_t channels, uint8_t bits)
{
    if (!s_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_rate == sample_rate) {
        return ESP_OK;
    }
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = bits,
        .channel = channels,
        .sample_rate = sample_rate,
    };
    if (s_rate) {
        esp_codec_dev_close(s_dev);
    }
    esp_err_t err = esp_codec_dev_open(s_dev, &fs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "codec open failed: %s", esp_err_to_name(err));
        return err;
    }
    s_rate = sample_rate;
    esp_codec_dev_set_out_vol(s_dev, 50);
    gpio_set_level(PIN_PA_EN, 1);
    ESP_LOGI(TAG, "stream open: %" PRIu32 " Hz / %d ch / %d bit", sample_rate, channels, bits);
    return ESP_OK;
}

esp_err_t audio_write(const void *data, size_t len)
{
    if (!s_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_codec_dev_write(s_dev, (void *)data, (int)len);
}

esp_err_t audio_set_volume(int vol)
{
    if (!s_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_codec_dev_set_out_vol(s_dev, vol);
}

esp_err_t audio_close(void)
{
    if (!s_dev) {
        return ESP_OK;
    }
    esp_codec_dev_close(s_dev);
    s_rate = 0;
    return ESP_OK;
}

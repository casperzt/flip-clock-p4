#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/* 板级硬件（Guition JC4880P443C / ESP32-P4）
 * I2C:  SDA=7  SCL=8
 * I2S:  MCLK=13 BCLK=12 LRCK=10 DOUT=9
 * 功放: NS4150 EN=11 (高有效)
 * 编解码: ES8311 @0x18
 */

esp_err_t audio_init(void);

/* 打开输出流（可重复调用，采样率变化时会重配） */
esp_err_t audio_open(uint32_t sample_rate, uint8_t channels, uint8_t bits);

/* 写入 PCM，阻塞直到写完 */
esp_err_t audio_write(const void *data, size_t len);

/* 0-100 */
esp_err_t audio_set_volume(int vol);

esp_err_t audio_close(void);

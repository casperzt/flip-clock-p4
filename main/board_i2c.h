#pragma once

#include "driver/i2c_master.h"

/* 全板唯一的一条 I2C 总线：SDA=7 SCL=8，ES8311 / GT911 / 摄像头共用 */
esp_err_t board_i2c_init(void);
i2c_master_bus_handle_t board_i2c_get(void);

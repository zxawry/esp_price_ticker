/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    i2c_device_config_t i2c_dev_conf;   /*!< Configuration for display device */
    uint8_t height;                     /*!< Display device height in pixels */
    uint8_t width;                      /*!< Display device width in pixels */
    uint8_t bytes_per_char;             /*!< Display device columns for each character */
} i2c_disp_config_t;

struct i2c_disp_t {
    i2c_master_dev_handle_t i2c_dev;    /*!< I2C device handle */
    uint8_t i2c_tmp_buffer[1 + 1 + 8];  /*!< I2C transaction buffer: cmd_phase_byte(1) + cmd_name_byte(1) + bytes_per_char(8) */
    uint8_t screen_buffer[128];         /*!< Display screen buffer: 16x8 charachters */
    uint8_t height;                     /*!< Display device height in pixels */
    uint8_t width;                      /*!< Display device width in pixels */
    uint8_t bytes_per_char;             /*!< Display device columns for each character */
};

typedef struct i2c_disp_t i2c_disp_t;

/* handle of Display device */
typedef struct i2c_disp_t *i2c_disp_handle_t;

/**
 * @brief Create a Display device.
 *
 * @param[in] bus_handle I2C master bus handle
 * @param[in] disp_config Configuration of Display
 * @param[out] disp_handle Handle of Display
 * @return ESP_OK: New success. ESP_FAIL: Not success.
 */
esp_err_t i2c_disp_new(i2c_master_bus_handle_t bus_handle, const i2c_disp_config_t *disp_config, i2c_disp_handle_t *disp_handle);

/**
 * @brief init a Display device.
 *
 * @param[in] disp_handle Display handle
 * @return ESP_OK: Init success. ESP_FAIL: Not success.
 */
esp_err_t i2c_disp_init(i2c_disp_handle_t disp_handle);

/**
 * @brief Power display on or off
 *
 * @param[in] disp_handle Display handle
 * @param[in] on_off True to turn on display
 * @return ESP_OK: Power success. Otherwise failed, please check I2C function fail reason.
 */
esp_err_t i2c_disp_power(i2c_disp_handle_t disp_handle, bool on_off);

/**
 * @brief Invert display on or off
 *
 * @param[in] disp_handle Display handle
 * @param[in] on_off True to invert display colour
 * @return ESP_OK: Invert success. Otherwise failed, please check I2C function fail reason.
 */
esp_err_t i2c_disp_invert(i2c_disp_handle_t disp_handle, bool on_off);

/**
 * @brief Clear Display
 *
 * @param[in] disp_handle Display handle
 * @param[in] row Position of row to start clearing
 * @param[in] col Position of column to start clearing
 * @return ESP_OK: Clear success. Otherwise failed, please check I2C function fail reason.
 */
esp_err_t i2c_disp_clear(i2c_disp_handle_t disp_handle, uint8_t row, uint8_t col);

/**
 * @brief Fill Display
 *
 * @param[in] disp_handle Display handle
 * @param[in] row Position of row to start clearing
 * @param[in] col Position of column to start clearing
 * @param[in] ch Character to fill Display screen with
 * @return ESP_OK: Fill success. Otherwise failed, please check I2C function fail reason.
 */
esp_err_t i2c_disp_fill(i2c_disp_handle_t disp_handle, uint8_t row, uint8_t col, char ch);

/**
 * @brief Write data to Display
 *
 * @param[in] disp_handle Display handle
 * @param[in] row Position of row to start writing
 * @param[in] col Position of column to start writing
 * @param[in] str String to write
 * @param[in] size String write size
 * @return ESP_OK: Write success. Otherwise failed, please check I2C function fail reason.
 */
esp_err_t i2c_disp_write(i2c_disp_handle_t disp_handle, uint8_t row, uint8_t col, const char *str, size_t size);

#ifdef __cplusplus
}
#endif

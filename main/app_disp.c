/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <string.h>
#include <stdio.h>
#include "sdkconfig.h"
#include "esp_types.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c_master.h"
#include "app_disp.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define I2C_DISP_BYTES_PER_CHAR (8)

// SSD1306 commands
#define SSD1306_CMD_SET_MEMORY_ADDR_MODE  0x20
#define SSD1306_CMD_SET_COLUMN_RANGE      0x21
#define SSD1306_CMD_SET_PAGE_RANGE        0x22
#define SSD1306_CMD_SET_CHARGE_PUMP       0x8D
#define SSD1306_CMD_MIRROR_X_OFF          0xA0
#define SSD1306_CMD_MIRROR_X_ON           0xA1
#define SSD1306_CMD_INVERT_OFF            0xA6
#define SSD1306_CMD_INVERT_ON             0xA7
#define SSD1306_CMD_SET_MULTIPLEX         0xA8
#define SSD1306_CMD_DISP_OFF              0xAE
#define SSD1306_CMD_DISP_ON               0xAF
#define SSD1306_CMD_MIRROR_Y_OFF          0xC0
#define SSD1306_CMD_MIRROR_Y_ON           0xC8
#define SSD1306_CMD_SET_COMPINS           0xDA

static const char TAG[] = "i2c_disp";

extern const uint8_t ASCII_FONT[][8];

static esp_err_t i2c_disp_transmit(i2c_disp_handle_t disp_handle, int cmd, const uint8_t *data, size_t size, bool is_cmd)
{
    ESP_RETURN_ON_FALSE(disp_handle, ESP_ERR_NO_MEM, TAG, "no mem for buffer");

    size_t len = 0;

    // fill control phase
    uint8_t control_phase_byte = 0;
    control_phase_byte |= ((data == NULL) & 0x01) << 7;     // if no data set Co=1
    control_phase_byte |= ((cmd < 0) & 0x01) << 6;          // if no cmd set D/C=1
    disp_handle->i2c_tmp_buffer[len++] = control_phase_byte;

    // fill command name
    if (is_cmd && (cmd >= 0)) {
        disp_handle->i2c_tmp_buffer[len++] = (uint8_t) cmd;
    }

    // fill data buffer
    if (data != NULL && size > 0) {
        ESP_RETURN_ON_FALSE(((len + size) < 10), ESP_ERR_NO_MEM, TAG, "buffer overflow");
        memcpy(disp_handle->i2c_tmp_buffer + len, data, size);
        len += size;
    }

    return i2c_master_transmit(disp_handle->i2c_dev, disp_handle->i2c_tmp_buffer, len, -1);
}

static esp_err_t i2c_disp_send_cmd(i2c_disp_handle_t disp_handle, int cmd, const uint8_t *data, size_t size)
{
    return i2c_disp_transmit(disp_handle, cmd, data, size, true);
}

static esp_err_t i2c_disp_send_data(i2c_disp_handle_t disp_handle, int cmd, const uint8_t *data, size_t size)
{
    return i2c_disp_transmit(disp_handle, cmd, data, size, false);
}

static esp_err_t i2c_disp_set_row_col(i2c_disp_handle_t disp_handle, uint8_t row, uint8_t col)
{
    ESP_RETURN_ON_ERROR(i2c_disp_send_cmd(disp_handle, SSD1306_CMD_SET_PAGE_RANGE, (uint8_t[]) {
        (row), (7),
    }, 2), TAG, "disp_handle send command SSD1306_CMD_SET_PAGE_RANGE failed");
    ESP_RETURN_ON_ERROR(i2c_disp_send_cmd(disp_handle, SSD1306_CMD_SET_COLUMN_RANGE, (uint8_t[]) {
        (col << 3), (127),
    }, 2), TAG, "disp_handle send command SSD1306_CMD_SET_COLUMN_RANGE failed");

    return ESP_OK;
}

esp_err_t i2c_disp_new(i2c_master_bus_handle_t bus_handle, const i2c_disp_config_t *disp_config, i2c_disp_handle_t *disp_handle)
{
    esp_err_t ret = ESP_OK;
    i2c_disp_handle_t out_handle;
    out_handle = (i2c_disp_handle_t)calloc(1, sizeof(*out_handle));
    ESP_GOTO_ON_FALSE(out_handle, ESP_ERR_NO_MEM, err, TAG, "no memory for i2c disp device");

    i2c_device_config_t i2c_dev_conf = {
        .scl_speed_hz = disp_config->i2c_dev_conf.scl_speed_hz,
        .device_address = disp_config->i2c_dev_conf.device_address,
    };

    if (out_handle->i2c_dev == NULL) {
        ESP_GOTO_ON_ERROR(i2c_master_bus_add_device(bus_handle, &i2c_dev_conf, &out_handle->i2c_dev), err, TAG, "i2c new bus failed");
    }

    memset(out_handle->screen_buffer, ' ', 128);

    out_handle->height = disp_config->height;
    out_handle->width = disp_config->width;
    out_handle->bytes_per_char = disp_config->bytes_per_char;
    *disp_handle = out_handle;

    return ESP_OK;

err:
    if (out_handle && out_handle->i2c_dev) {
        i2c_master_bus_rm_device(out_handle->i2c_dev);
    }
    free(out_handle);
    return ret;
}

esp_err_t i2c_disp_init(i2c_disp_handle_t disp_handle)
{
    ESP_RETURN_ON_ERROR(i2c_disp_send_cmd(disp_handle, SSD1306_CMD_SET_MULTIPLEX, (uint8_t[]) {
        63 // set multiplex ratio
    }, 1), TAG, "disp_handle send command SSD1306_CMD_SET_MULTIPLEX failed");
    ESP_RETURN_ON_ERROR(i2c_disp_send_cmd(disp_handle, SSD1306_CMD_SET_COMPINS, (uint8_t[1]) {
        0x12 // set COM pins hardware configuration
    }, 1), TAG, "disp_handle send command SSD1306_CMD_SET_COMPINS failed");
    ESP_RETURN_ON_ERROR(i2c_disp_send_cmd(disp_handle, SSD1306_CMD_DISP_OFF, NULL, 0), TAG,
                        "disp_handle send command SSD1306_CMD_DISP_OFF failed");
    ESP_RETURN_ON_ERROR(i2c_disp_send_cmd(disp_handle, SSD1306_CMD_SET_MEMORY_ADDR_MODE, (uint8_t[]) {
        0x00 // horizontal addressing mode
    }, 1), TAG, "disp_handle send command SSD1306_CMD_SET_MEMORY_ADDR_MODE failed");
    ESP_RETURN_ON_ERROR(i2c_disp_send_cmd(disp_handle, SSD1306_CMD_SET_CHARGE_PUMP, (uint8_t[]) {
        0x14 // enable charge pump
    }, 1), TAG, "disp_handle send command SSD1306_CMD_SET_CHARGE_PUMP failed");
    ESP_RETURN_ON_ERROR(i2c_disp_send_cmd(disp_handle, SSD1306_CMD_MIRROR_X_OFF, NULL, 0), TAG,
                        "disp_handle send command SSD1306_CMD_MIRROR_X_OFF failed");
    ESP_RETURN_ON_ERROR(i2c_disp_send_cmd(disp_handle, SSD1306_CMD_MIRROR_Y_OFF, NULL, 0), TAG,
                        "disp_handle send command SSD1306_CMD_MIRROR_Y_OFF failed");
    return ESP_OK;
}

esp_err_t i2c_disp_power(i2c_disp_handle_t disp_handle, bool on_off)
{
    int command = 0;
    if (on_off) {
        command = SSD1306_CMD_DISP_ON;
    } else {
        command = SSD1306_CMD_DISP_OFF;
    }
    ESP_RETURN_ON_ERROR(i2c_disp_send_cmd(disp_handle, command, NULL, 0), TAG,
                        "disp_handle send command SSD1306_CMD_DISP_ON/OFF failed");
    // SEG/COM will be ON/OFF after 100ms after sending DISP_ON/OFF command
    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

esp_err_t i2c_disp_invert(i2c_disp_handle_t disp_handle, bool on_off)
{
    int command = 0;
    if (on_off) {
        command = SSD1306_CMD_INVERT_ON;
    } else {
        command = SSD1306_CMD_INVERT_OFF;
    }
    ESP_RETURN_ON_ERROR(i2c_disp_send_cmd(disp_handle, command, NULL, 0), TAG,
                        "disp_handle send command SSD1306_CMD_INVERT_ON/OFF failed");
    return ESP_OK;
}

esp_err_t i2c_disp_clear(i2c_disp_handle_t disp_handle, uint8_t row, uint8_t col)
{
    return i2c_disp_fill(disp_handle, row, col, ' ');
}

esp_err_t i2c_disp_fill(i2c_disp_handle_t disp_handle, uint8_t row, uint8_t col, char ch)
{
    ESP_RETURN_ON_FALSE((row < 8 && col < 16), ESP_ERR_INVALID_ARG, TAG, "invalid row or column range");
    ESP_RETURN_ON_FALSE((ch - 32 >= 0 && ch - 32 < 96), ESP_ERR_INVALID_ARG, TAG, "invalid ascii character");

    ESP_RETURN_ON_ERROR(i2c_disp_set_row_col(disp_handle, row, col), TAG,
            "disp_handle set row and column failed");

    const uint8_t *data = ASCII_FONT[ch - 32];

    uint8_t start = (row * 16) + col;
    uint8_t end = 128;

    for (uint8_t i = start; i < end; i++) {
        ESP_RETURN_ON_ERROR(i2c_disp_send_data(disp_handle, -1, data, 8), TAG,
                "disp_handle send data failed");
        disp_handle->screen_buffer[i] = (uint8_t) ch;
    }

    return ESP_OK;
}

// TODO: this function looks very ugly!
// perhaps first make the flow more clear
// then it may be more efficient to only
// update `screen_buffer` with new characters
// in this function and then use a seperate
// internal function like `i2c_disp_redraw`
// to update the altered rows and columns
// on the display.
esp_err_t i2c_disp_write(i2c_disp_handle_t disp_handle, uint8_t row, uint8_t col, const char *str, size_t size)
{
    ESP_RETURN_ON_FALSE((row < 8 && col < 16), ESP_ERR_INVALID_ARG, TAG, "invalid row or column range");
    for (uint8_t i = 0; i < strlen(str); i++) {
        ESP_RETURN_ON_FALSE((str[i] - 32 >= 0 && str[i] - 32 < 96), ESP_ERR_INVALID_ARG, TAG, "invalid ascii character");
    }

    ESP_RETURN_ON_ERROR(i2c_disp_set_row_col(disp_handle, row, col), TAG,
            "disp_handle set row and column failed");

    uint8_t start = (row * 16) + col;
    uint8_t end = ((start + size) > 128) ? 128 : start + size;

    for (uint8_t i = start, j = 0; (i < end && j < size); i++, j++) {
        char ch = str[j];
        if (ch == '\0') {
            j--; // do not advance j;
            ch = ' ';
        }
        // here we could compare the value with
        // the vaule in screen_buffer and only
        // write to display in case a new character
        // is meant.
        const uint8_t *data = ASCII_FONT[ch - 32];
        ESP_RETURN_ON_ERROR(i2c_disp_send_data(disp_handle, -1, data, 8), TAG,
                "disp_handle send data failed");
        disp_handle->screen_buffer[i] = (uint8_t) ch;
    }

    return ESP_OK;
}

/*
 * file: app_main.c
 * author: zxawry
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "driver/i2c_master.h"
#include "app_disp.h"

static const char *TAG = "APP_MAIN";

extern const uint8_t ASCII_FONT[][8];

#define DISP_LCD_PIXEL_CLOCK_HZ    (400 * 1000)
#define DISP_PIN_NUM_SDA           21
#define DISP_PIN_NUM_SCL           22
#define DISP_PIN_NUM_RST           -1
#define DISP_I2C_HW_ADDR           0x3C
#define DISP_I2C_BUS_PORT          0
#define DISP_LCD_H_RES             128
#define DISP_LCD_V_RES             64
#define DISP_LCD_CMD_BITS          8
#define DISP_LCD_PARAM_BITS        8

void app_main(void)
{
    ESP_LOGI(TAG, "Initialise I2C bus");
    i2c_master_bus_handle_t bus_handle = NULL;
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .i2c_port = DISP_I2C_BUS_PORT,
        .sda_io_num = DISP_PIN_NUM_SDA,
        .scl_io_num = DISP_PIN_NUM_SCL,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));

    ESP_LOGI(TAG, "Initialise I2C display");
    i2c_disp_handle_t disp_handle = NULL;
    i2c_disp_config_t disp_config = {
        .i2c_dev_conf.scl_speed_hz = DISP_LCD_PIXEL_CLOCK_HZ,
        .i2c_dev_conf.device_address = DISP_I2C_HW_ADDR,
        .height = DISP_LCD_V_RES,
        .width = DISP_LCD_H_RES,
        .bytes_per_char = 8,
    };
    ESP_ERROR_CHECK(i2c_disp_new(bus_handle, &disp_config, &disp_handle));

    ESP_ERROR_CHECK(i2c_disp_init(disp_handle));
    ESP_ERROR_CHECK(i2c_disp_power(disp_handle, true));

    ESP_ERROR_CHECK(i2c_disp_invert(disp_handle, true));
    vTaskDelay(2 * 1000 / portTICK_PERIOD_MS);
    ESP_ERROR_CHECK(i2c_disp_invert(disp_handle, false));

    ESP_ERROR_CHECK(i2c_disp_fill(disp_handle, 0, 0, '!'));
    vTaskDelay(2 * 1000 / portTICK_PERIOD_MS);
    ESP_ERROR_CHECK(i2c_disp_clear(disp_handle, 0, 0));

    ESP_ERROR_CHECK(i2c_disp_fill(disp_handle, 4, 8, '@'));
    vTaskDelay(2 * 1000 / portTICK_PERIOD_MS);
    ESP_ERROR_CHECK(i2c_disp_clear(disp_handle, 5, 8));
    vTaskDelay(2 * 1000 / portTICK_PERIOD_MS);
    ESP_ERROR_CHECK(i2c_disp_write(disp_handle, 6, 0, "Life is Pleasant", 16));
    vTaskDelay(2 * 1000 / portTICK_PERIOD_MS);
    ESP_ERROR_CHECK(i2c_disp_write(disp_handle, 6, 0, "Life is Good", 16));
    vTaskDelay(2 * 1000 / portTICK_PERIOD_MS);
    ESP_ERROR_CHECK(i2c_disp_clear(disp_handle, 0, 0));

    char mydata[128];
    for (uint8_t i = 0; i < 96; i++) {
        mydata[i] = i + 32;
    }
    mydata[96] = '\0';

    ESP_ERROR_CHECK(i2c_disp_write(disp_handle, 0, 0, mydata, 96));
    vTaskDelay(2 * 1000 / portTICK_PERIOD_MS);
    ESP_ERROR_CHECK(i2c_disp_clear(disp_handle, 0, 0));

    char number[16];
    uint32_t counter = 0x1337c0d3;

    while (1) {
        snprintf(number, 16, "%lx", counter);
        ESP_LOGI(TAG, "counter=0x%x", counter);
        ESP_ERROR_CHECK(i2c_disp_write(disp_handle, 0, 0, "counter:", 8));
        ESP_ERROR_CHECK(i2c_disp_write(disp_handle, 2, 0, number, 16));
        counter >>= 8;
        if (counter == 0x00) {
            counter = 0x1337c0d3;
        }
        vTaskDelay(1 * 1000 / portTICK_PERIOD_MS);
    }
}

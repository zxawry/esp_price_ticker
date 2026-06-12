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

#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

static const char *TAG = "app_main";

extern esp_err_t wifi_init_sta(void);

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
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

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
    ESP_ERROR_CHECK(i2c_disp_invert(disp_handle, false));
    ESP_ERROR_CHECK(i2c_disp_clear(disp_handle, 0, 0));

    ESP_LOGI(TAG, "Initialise NVS");
    ESP_ERROR_CHECK(i2c_disp_write(disp_handle, 0, 0, "Initialise NVS", 16));
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // uncomment if more wifi logs are needed
    //if (CONFIG_LOG_MAXIMUM_LEVEL > CONFIG_LOG_DEFAULT_LEVEL) {
        //esp_log_level_set("wifi", CONFIG_LOG_MAXIMUM_LEVEL);
    //}

    ESP_LOGI(TAG, "Initialise WiFi");
    ESP_ERROR_CHECK(i2c_disp_write(disp_handle, 0, 0, "Initialise WiFi", 16));
    ESP_ERROR_CHECK(wifi_init_sta());

    ESP_ERROR_CHECK(i2c_disp_clear(disp_handle, 0, 0));
}

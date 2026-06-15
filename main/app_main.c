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

#include "esp_check.h"

#include "app_comm.h"

static const char *TAG = "app_main";

extern const uint8_t ASCII_FONT[][8];

extern esp_err_t wifi_init_sta(void);

extern esp_err_t http_request(market_handle_t market_handle);

const char *MARKETS[] = { "BTCUSDT", "XMRUSDT", "LTCUSDT", "USDTIRT" };

typedef struct {
    market_handle_t market_handle;
    i2c_disp_handle_t disp_handle;
} app_context_t;

void http_get_task(void *pvParameters)
{
    app_context_t *context = (app_context_t *)pvParameters;

    while (1) {
        market_t *market = context->market_handle;

        // iterate markets
        while (market != NULL) {
            esp_err_t err = http_request(market);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "last_price = %s", market->last_price);
                ESP_LOGI(TAG, "last_update = %s", market->last_update);
            }

            market = market->next;

            vTaskDelay(2 * 1000 / portTICK_PERIOD_MS);
        }

        vTaskDelay(60 * 1000 / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}

void disp_put_task(void *pvParameters)
{
    app_context_t *context = (app_context_t *)pvParameters;

    while (1) {
        market_t *market = context->market_handle;

        // iterate markets
        while (market != NULL) {
            ESP_ERROR_CHECK(i2c_disp_write(context->disp_handle, 0, 0, market->symbol, 16));
            if (market->last_price[0]) {
                ESP_ERROR_CHECK(i2c_disp_write(context->disp_handle, 2, 0, market->last_price, 16));
            } else {
                ESP_ERROR_CHECK(i2c_disp_write(context->disp_handle, 2, 0, "NaN", 16));
            }
            if (market->last_update[0]) {
                ESP_ERROR_CHECK(i2c_disp_write(context->disp_handle, 4, 0, market->last_update, 16));
            } else {
                ESP_ERROR_CHECK(i2c_disp_write(context->disp_handle, 4, 0, "NaN", 16));
            }

            market = market->next;

            vTaskDelay(10 * 1000 / portTICK_PERIOD_MS);
        }
    }
    vTaskDelete(NULL);
}

static esp_err_t market_new(market_handle_t *market_handle, const char *symbol)
{
    esp_err_t ret = ESP_OK;

    market_handle_t out_handle;
    out_handle = (market_handle_t)calloc(1, sizeof(*out_handle));
    ESP_GOTO_ON_FALSE(out_handle, ESP_ERR_NO_MEM, err, TAG, "No memory for for %s", symbol);

    memset(out_handle->symbol, 0, 16);
    strcpy(out_handle->symbol, symbol);
    memset(out_handle->last_price, 0, 16);
    memset(out_handle->last_update, 0, 16);
    out_handle->next = NULL;

    *market_handle = out_handle;

    return ESP_OK;

err:
    free(out_handle);
    return ret;
}

static esp_err_t app_init(app_context_t *context)
{
    esp_err_t ret = ESP_OK;

    // required for WiFi init
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
    context->disp_handle = disp_handle;

    ESP_LOGI(TAG, "Initialise NVS");
    ESP_ERROR_CHECK(i2c_disp_write(disp_handle, 0, 0, "Initialise NVS", 16));
    ESP_LOGI(TAG, "Initialise NVS");
    ret = nvs_flash_init();
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

    ESP_LOGI(TAG, "Initialise Markets");
    // create head item for markets list in context
    context->market_handle = NULL;
    ESP_ERROR_CHECK(market_new(&context->market_handle, "ETHUSDT"));

    market_handle_t market_prev = context->market_handle;
    for (uint8_t i = 0; i < 4; i++) {
        market_handle_t market_temp = NULL;
        ESP_ERROR_CHECK(market_new(&market_temp, MARKETS[i]));

        market_prev->next = market_temp;
        market_prev = market_temp;
    }

    return ret;
}

void app_main(void)
{
    static app_context_t context = {0};

    ESP_LOGI(TAG, "Initialise App");
    ESP_ERROR_CHECK(app_init(&context));

    ESP_LOGI(TAG, "Create Tasks");
    xTaskCreate(http_get_task, "http_get_task", 4096, &context, 20, NULL);
    xTaskCreate(disp_put_task, "disp_put_task", 4096, &context, 20, NULL);
}

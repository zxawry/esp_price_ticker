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

#include "freertos/queue.h"
#include "driver/gpio.h"

static const char *TAG = "app_main";

extern esp_err_t wifi_init_sta(void);

extern esp_err_t http_request(market_handle_t market_handle);

extern esp_err_t market_new(market_handle_t *market_handle, const char *symbol);
extern market_t *market_search(market_t *market, bool forward);
extern void stat_print(i2c_disp_handle_t disp_handle, market_t *market, bool first_run);
extern void stat_empty(i2c_disp_handle_t disp_handle, bool first_run);
extern void stat_toggle(i2c_disp_handle_t disp_handle, bool is_paused);
extern void menu_print(i2c_disp_handle_t disp_handle, market_t *market, uint8_t count, uint8_t selector, bool first_run);
extern void menu_backward(i2c_disp_handle_t disp_handle, market_t *market, uint8_t count, uint8_t selector);
extern void menu_forward(i2c_disp_handle_t disp_handle, market_t *market, uint8_t count, uint8_t selector);
extern void menu_toggle(i2c_disp_handle_t disp_handle, market_t *market, uint8_t selector);

#define MARKETS_COUNT (11)

const char *MARKETS[] = {
    "BTCUSDT", "XMRUSDT", "LTCUSDT", "ETHUSDT", "ZECUSDT", "ETCUSDT", "TONUSDT", "XRPUSDT", "BATUSDT", "DAIUSDT", "USDTIRT",
};

typedef struct {
    market_handle_t market_head;
    market_handle_t market_tail;
    uint8_t market_count;
    QueueHandle_t key_event_queue;
    TaskHandle_t stat_task_handle;
    TaskHandle_t menu_task_handle;
    i2c_disp_handle_t disp_handle;
} app_context_t;

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    app_context_t *context = (app_context_t *)arg;
    if (gpio_get_level(GPIO_KEY_MENU) == 0) {
        key_event_t evt = KEY_MENU;
        xQueueSendFromISR(context->key_event_queue, &evt, NULL);
    }
    if (gpio_get_level(GPIO_KEY_PREV) == 0) {
        key_event_t evt = KEY_PREV;
        xQueueSendFromISR(context->key_event_queue, &evt, NULL);
    }
    if (gpio_get_level(GPIO_KEY_NEXT) == 0) {
        key_event_t evt = KEY_NEXT;
        xQueueSendFromISR(context->key_event_queue, &evt, NULL);
    }
    if (gpio_get_level(GPIO_KEY_FLOW) == 0) {
        key_event_t evt = KEY_FLOW;
        xQueueSendFromISR(context->key_event_queue, &evt, NULL);
    }
}

static void http_task(void *pvParameters)
{
    app_context_t *context = (app_context_t *)pvParameters;
    market_t *market = context->market_head;

    while (1) {
        if (market->is_enabled) {
            esp_err_t err = http_request(market);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "http: %s = %s @ %s",
                        market->symbol, market->last_price, market->last_update);
            }
        } else { // market is disabled
            ESP_LOGI(TAG, "http: %s = %d", market->symbol, market->is_enabled);
        }
        market = market->next;

        // check if a full iteration is complete
        if (market == context->market_head) {
            // wait longer to avoid rate limits
            ESP_LOGI(TAG, "http: suspend");
            vTaskDelay(60 * 1000 / portTICK_PERIOD_MS);
        } else {
            vTaskDelay(1 * 1000 / portTICK_PERIOD_MS);
        }
    }
    vTaskDelete(NULL);
}

static void stat_task(void *pvParameters)
{
    app_context_t *context = (app_context_t *)pvParameters;
    market_t *market = context->market_head;

    // don't fret, this just means
    // we have no enabled markets
    bool is_zombie = false;
    bool is_paused = false;

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        // switched to stat
        ESP_LOGI(TAG, "stat: resumed");
        // search from the tail so we show head if it's enabled too
        market = market_search(context->market_tail, true);
        if (market == NULL) {
            is_zombie = true;
            stat_empty(context->disp_handle, true);
            ESP_LOGI(TAG, "stat: zombie");
        } else {
            is_zombie = false;
            stat_print(context->disp_handle, market, true);
            // restore previous pause state on the display
            stat_toggle(context->disp_handle, is_paused);
            ESP_LOGI(TAG, "stat: %s = %d", market->symbol, market->is_enabled);
        }

        key_event_t evt;
        while (xQueueReceive(context->key_event_queue, &evt, pdMS_TO_TICKS(2 * 1000)) || pdTRUE) {
            if (evt == KEY_MENU && gpio_get_level(GPIO_KEY_MENU) == 0) {
                ESP_LOGI(TAG, "stat: suspend");
                xTaskNotifyGive(context->menu_task_handle);
                goto wait_for_stat;
            } else if (evt == KEY_PREV && gpio_get_level(GPIO_KEY_PREV) == 0) {
                if (!is_zombie) {
                    market = market_search(market, false);
                    stat_print(context->disp_handle, market, false);
                    ESP_LOGI(TAG, "stat: %s = %d", market->symbol, market->is_enabled);
                } else {
                    stat_empty(context->disp_handle, false);
                    ESP_LOGI(TAG, "stat: zombie");
                }
            } else if (evt == KEY_NEXT && gpio_get_level(GPIO_KEY_NEXT) == 0) {
                if (!is_zombie) {
                    market = market_search(market, true);
                    stat_print(context->disp_handle, market, false);
                    ESP_LOGI(TAG, "stat: %s = %d", market->symbol, market->is_enabled);
                } else {
                    stat_empty(context->disp_handle, false);
                    ESP_LOGI(TAG, "stat: zombie");
                }
            } else if (evt == KEY_FLOW && gpio_get_level(GPIO_KEY_FLOW) == 0) {
                is_paused = !is_paused;
                stat_toggle(context->disp_handle, is_paused);
                ESP_LOGI(TAG, "stat: is_paused %d", is_paused);
            } else {
                //ESP_LOGI(TAG, "stat: timeout");
                if (!is_zombie) {
                    if (!is_paused) {
                        market = market_search(market, true);
                    }
                    stat_print(context->disp_handle, market, false);
                    ESP_LOGI(TAG, "stat: %s = %d", market->symbol, market->is_enabled);
                } else { // we are zombie
                    stat_empty(context->disp_handle, false);
                    ESP_LOGI(TAG, "stat: zombie");
                }
            }
        }
wait_for_stat:
    }
    vTaskDelete(NULL);
}

static void menu_task(void *pvParameters)
{
    app_context_t *context = (app_context_t *)pvParameters;
    market_t *market = context->market_head;

    uint8_t selector = 0;

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        // switched to menu
        ESP_LOGI(TAG, "menu: resumed");
        market = context->market_head;
        selector = 0;
        menu_print(context->disp_handle, market, context->market_count, selector, true);
        do {
            ESP_LOGI(TAG, "menu: %s = %d", market->symbol, market->is_enabled);
            market = market->next;
        } while (market != context->market_head);

        key_event_t evt;
        while (xQueueReceive(context->key_event_queue, &evt, pdMS_TO_TICKS(2 * 1000)) || pdTRUE) {
            if (evt == KEY_MENU && gpio_get_level(GPIO_KEY_MENU) == 0) {
                ESP_LOGI(TAG, "menu: suspend");
                xTaskNotifyGive(context->stat_task_handle);
                goto wait_for_menu;
            } else if (evt == KEY_PREV && gpio_get_level(GPIO_KEY_PREV) == 0) {
                market = market->prev;
                selector = (selector == 0) ? context->market_count - 1 : selector - 1;
                ESP_LOGI(TAG, "menu: selector = %d", selector);
                menu_backward(context->disp_handle, market, context->market_count, selector);
                ESP_LOGI(TAG, "menu: %s = %d", market->symbol, market->is_enabled);
            } else if (evt == KEY_NEXT && gpio_get_level(GPIO_KEY_NEXT) == 0) {
                market = market->next;
                selector = (selector == context->market_count - 1) ? 0 : selector + 1;
                menu_forward(context->disp_handle, market, context->market_count, selector);
                ESP_LOGI(TAG, "menu: %s = %d", market->symbol, market->is_enabled);
            } else if (evt == KEY_FLOW && gpio_get_level(GPIO_KEY_FLOW) == 0) {
                market->is_enabled = !market->is_enabled;
                menu_toggle(context->disp_handle, market, selector);
                ESP_LOGI(TAG, "menu: %s = %d", market->symbol, market->is_enabled);
            } else {
                //ESP_LOGI(TAG, "menu: timeout");
            }
        }
wait_for_menu:
    }
    vTaskDelete(NULL);
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
    market_handle_t market_temp = NULL;
    ESP_ERROR_CHECK(market_new(&market_temp, MARKETS[0]));
    context->market_head = market_temp;

    market_handle_t market_prev = context->market_head;
    for (uint8_t i = 1; i < MARKETS_COUNT; i++) {
        market_temp = NULL;
        ESP_ERROR_CHECK(market_new(&market_temp, MARKETS[i]));

        market_prev->next = market_temp;
        market_temp->prev = market_prev;
        market_prev = market_temp;
    }
    context->market_tail = market_temp;
    context->market_count = MARKETS_COUNT;

    // make the liked list circular
    context->market_head->prev = context->market_tail;
    context->market_tail->next = context->market_head;

    ESP_LOGI(TAG, "Initialise GPIOs");
    gpio_config_t io_config = {
        .intr_type = GPIO_INTR_NEGEDGE, //interrupt of falling edge
        .mode = GPIO_MODE_INPUT, //set as input mode
        .pin_bit_mask = GPIO_KEYS_MASK, //bit mask of the pins to set
        .pull_down_en = 0, //disable pull-down mode
        .pull_up_en = 1, //enable pull-up mode
    };
    gpio_config(&io_config);

    //create a queue to handle key event from isr
    context->key_event_queue = xQueueCreate(16, sizeof(key_event_t));
    ESP_RETURN_ON_FALSE(context->key_event_queue, ESP_ERR_NO_MEM, TAG, "no mem for key queue");

    //install gpio isr service
    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT));
    //hook isr handler for specific gpio pin
    ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_KEY_MENU, gpio_isr_handler, (void*) context));
    ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_KEY_PREV, gpio_isr_handler, (void*) context));
    ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_KEY_NEXT, gpio_isr_handler, (void*) context));
    ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_KEY_FLOW, gpio_isr_handler, (void*) context));

    return ret;
}

void app_main(void)
{
    static app_context_t context = {0};

    ESP_LOGI(TAG, "Initialise App");
    ESP_ERROR_CHECK(app_init(&context));

    ESP_LOGI(TAG, "Create Tasks");
    xTaskCreate(http_task, "http_task", 4096, &context, 20, NULL);
    xTaskCreate(stat_task, "stat_task", 4096, &context, 20, &(context.stat_task_handle));
    xTaskCreate(menu_task, "menu_task", 4096, &context, 20, &(context.menu_task_handle));

    xTaskNotifyGive(context.stat_task_handle);
}

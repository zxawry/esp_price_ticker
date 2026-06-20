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

#define GPIO_INPUT_IO_0     (16) //CONFIG_GPIO_INPUT_0
#define GPIO_INPUT_IO_1     (17) //CONFIG_GPIO_INPUT_1
#define GPIO_INPUT_IO_2     (18) //CONFIG_GPIO_INPUT_2
#define GPIO_INPUT_IO_3     (19) //CONFIG_GPIO_INPUT_3
#define GPIO_INPUT_PIN_SEL  ((1ULL<<GPIO_INPUT_IO_0) | (1ULL<<GPIO_INPUT_IO_1) | (1ULL<<GPIO_INPUT_IO_2) | (1ULL<<GPIO_INPUT_IO_3))
#define ESP_INTR_FLAG_DEFAULT 0

static const char *TAG = "app_main";

extern const uint8_t ASCII_FONT[][8];

extern esp_err_t wifi_init_sta(void);

extern esp_err_t http_request(market_handle_t market_handle);

const char *MARKETS[] = { "BTCUSDT", "XMRUSDT", "LTCUSDT", "USDTIRT" };

typedef struct {
    market_handle_t market_head;
    market_handle_t market_tail;
    uint8_t selector;
    bool show_menu;
    bool is_paused;
    QueueHandle_t cmd_event_queue;
    QueueHandle_t key_event_queue;
    i2c_disp_handle_t disp_handle;
} app_context_t;

typedef enum {
    KEY_MENU_PRESS,
    KEY_PREV_PRESS,
    KEY_NEXT_PRESS,
    KEY_FLOW_PRESS,
} key_event_t;

typedef enum {
    CMD_MENU_MARKET,
    CMD_PREV_MARKET,
    CMD_NEXT_MARKET,
    CMD_FLOW_MARKET,
} cmd_event_t;

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    app_context_t *context = (app_context_t *)arg;
    if (gpio_get_level(GPIO_INPUT_IO_0) == 0) {
        key_event_t evt = KEY_MENU_PRESS;
        xQueueSendFromISR(context->key_event_queue, &evt, NULL);
    }
    if (gpio_get_level(GPIO_INPUT_IO_1) == 0) {
        key_event_t evt = KEY_PREV_PRESS;
        xQueueSendFromISR(context->key_event_queue, &evt, NULL);
    }
    if (gpio_get_level(GPIO_INPUT_IO_2) == 0) {
        key_event_t evt = KEY_NEXT_PRESS;
        xQueueSendFromISR(context->key_event_queue, &evt, NULL);
    }
    if (gpio_get_level(GPIO_INPUT_IO_3) == 0) {
        key_event_t evt = KEY_FLOW_PRESS;
        xQueueSendFromISR(context->key_event_queue, &evt, NULL);
    }
}

void gpio_get_task(void *pvParameters)
{
    app_context_t *context = (app_context_t *)pvParameters;

    while (1) {
        key_event_t evt;
        if (xQueueReceive(context->key_event_queue, &evt, portMAX_DELAY) == pdTRUE) {
            switch (evt) {
                case KEY_MENU_PRESS:
                    if (gpio_get_level(GPIO_INPUT_IO_0) == 0) {
                        cmd_event_t cmd = CMD_MENU_MARKET;
                        xQueueSend(context->cmd_event_queue, &cmd, 0);
                    }
                    break;
                case KEY_PREV_PRESS:
                    if (gpio_get_level(GPIO_INPUT_IO_1) == 0) {
                        cmd_event_t cmd = CMD_PREV_MARKET;
                        xQueueSend(context->cmd_event_queue, &cmd, 0);
                    }
                    break;
                case KEY_NEXT_PRESS:
                    if (gpio_get_level(GPIO_INPUT_IO_2) == 0) {
                        cmd_event_t cmd = CMD_NEXT_MARKET;
                        xQueueSend(context->cmd_event_queue, &cmd, 0);
                    }
                    break;
                case KEY_FLOW_PRESS:
                    if (gpio_get_level(GPIO_INPUT_IO_3) == 0) {
                        cmd_event_t cmd = CMD_FLOW_MARKET;
                        xQueueSend(context->cmd_event_queue, &cmd, 0);
                    }
                    break;
                default:
                    ESP_LOGE(TAG, "Invalid key event");
                    break;
            }
        }
    }
    vTaskDelete(NULL);
}

void http_get_task(void *pvParameters)
{
    app_context_t *context = (app_context_t *)pvParameters;

    // iterate markets
    market_t *market = context->market_head;
    while (1) {
        if (market->is_enabled) {
            esp_err_t err = http_request(market);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "symbol = %s", market->symbol);
                ESP_LOGI(TAG, "last_price = %s", market->last_price);
                ESP_LOGI(TAG, "last_update = %s", market->last_update);
            }
        } else { // market is disabled
            ESP_LOGI(TAG, "%s is disabled, skipping", market->symbol);
        }
        market = market->next;

        // check if a full iteration is complete
        if (market == context->market_head) {
            // wait longer to avoid rate limits
            ESP_LOGI(TAG, "paused for rate limits");
            vTaskDelay(60 * 1000 / portTICK_PERIOD_MS);
        } else {
            vTaskDelay(1 * 1000 / portTICK_PERIOD_MS);
        }
    }
    vTaskDelete(NULL);
}

static market_t *market_search(market_t *market, bool forward)
{
    market_t *market_head = market;
    do {
        market = forward ? market->next : market->prev;
        if (market->is_enabled) {
            return market;
        }
    } while (market != market_head);

    return NULL;
}

static esp_err_t stat_print(i2c_disp_handle_t disp_handle, market_t *market, bool clear)
{
    if (market == NULL) {
        ESP_ERROR_CHECK(i2c_disp_write(disp_handle, 0, 0, "No Valid Markets", clear ? 128 : 16));
    } else {
        ESP_ERROR_CHECK(i2c_disp_write(disp_handle, 0, 0, market->symbol, clear ? 128 : 16));
        ESP_ERROR_CHECK(i2c_disp_write(disp_handle, 2, 0, *market->last_price ? market->last_price : "NaN", 16));
        ESP_ERROR_CHECK(i2c_disp_write(disp_handle, 4, 0, *market->last_update ? market->last_update : "NaN", 16));
    }
    return ESP_OK;
}

static esp_err_t menu_print(i2c_disp_handle_t disp_handle, market_t *market, bool clear)
{
    ESP_ERROR_CHECK(i2c_disp_write(disp_handle, 0, 0, "Choose Markets:", clear ? 128: 16));

    uint8_t row = 2; // MARKETS_LIST_OFFSET
    market_t *market_head = market;
    do {
        char status[5] = { '[', market->is_enabled ? '*' : ' ', ']', ' ', '\0' };
        ESP_ERROR_CHECK(i2c_disp_write(disp_handle, row, 0, status, 4));
        ESP_ERROR_CHECK(i2c_disp_write(disp_handle, row, 4, market->symbol, 12));
        market = market->next;
        row++;
    } while (market != market_head);
    ESP_ERROR_CHECK(i2c_disp_highlight(disp_handle, true, 2, 0, 16));

    return ESP_OK;
}

static esp_err_t menu_select(i2c_disp_handle_t disp_handle, market_t *market, uint8_t selector, bool next)
{
    ESP_ERROR_CHECK(i2c_disp_highlight(disp_handle, false, selector, 0, 16));
    selector = next ?
        ((selector == 6) ? 2 : selector + 1): // next
        ((selector == 2) ? 6 : selector - 1); // prev
    ESP_ERROR_CHECK(i2c_disp_highlight(disp_handle, true, selector, 0, 16));

    return ESP_OK;
}

static esp_err_t menu_toggle(i2c_disp_handle_t disp_handle, market_t *market, uint8_t selector)
{
    ESP_ERROR_CHECK(i2c_disp_write(disp_handle, selector, 1, market->is_enabled ? "*" : " ", 1));
    ESP_ERROR_CHECK(i2c_disp_highlight(disp_handle, true, selector, 1, 1));
    return ESP_OK;
}

void disp_put_task(void *pvParameters)
{
    app_context_t *context = (app_context_t *)pvParameters;
    market_t *market = context->market_head;

    ESP_ERROR_CHECK(i2c_disp_write(context->disp_handle, 0, 0, "Fetching Markets", 16));

    while (1) {
        cmd_event_t cmd;
        if (xQueueReceive(context->cmd_event_queue, &cmd, pdMS_TO_TICKS(2 * 1000)) == pdTRUE) {
            switch (cmd) {
                case CMD_MENU_MARKET:
                    context->show_menu = !context->show_menu;
                    if (context->show_menu) {
                        market = context->market_head; // reset iterator
                        ESP_ERROR_CHECK(menu_print(context->disp_handle, market, true));
                        context->selector = 2;
                    } else { // show stat
                        market = market_search(market, true);
                        ESP_ERROR_CHECK(stat_print(context->disp_handle, market, true));
                        if (market == NULL) { market = context->market_head; };
                        context->selector = 0;
                    }
                    break;
                case CMD_PREV_MARKET:
                    if (context->show_menu) {
                        // select previous market
                        market = market->prev;
                        ESP_ERROR_CHECK(menu_select(context->disp_handle, market, context->selector, false));
                        context->selector = (context->selector == 2) ? 6 : context->selector - 1;
                    } else {
                        market = market_search(market, false);
                        ESP_ERROR_CHECK(stat_print(context->disp_handle, market, false));
                        if (market == NULL) { market = context->market_head; };
                    }
                    break;
                case CMD_NEXT_MARKET:
                    if (context->show_menu) {
                        // select next market
                        market = market->next;
                        ESP_ERROR_CHECK(menu_select(context->disp_handle, market, context->selector, true));
                        context->selector = (context->selector == 6) ? 2 : context->selector + 1;
                    } else {
                        market = market_search(market, true);
                        ESP_ERROR_CHECK(stat_print(context->disp_handle, market, false));
                        if (market == NULL) { market = context->market_head; };
                    }
                    break;
                case CMD_FLOW_MARKET:
                    if (context->show_menu) {
                        // toggle current market
                        market->is_enabled = !market->is_enabled;
                        ESP_ERROR_CHECK(menu_toggle(context->disp_handle, market, context->selector));
                        ESP_LOGI(TAG, "%s is_enabled = %d", market->symbol, market->is_enabled);
                    } else {
                        context->is_paused = !context->is_paused;
                        if (context->is_paused) {
                            ESP_ERROR_CHECK(i2c_disp_write(context->disp_handle, 7, 15, "P", 1));
                        } else {
                            ESP_ERROR_CHECK(i2c_disp_write(context->disp_handle, 7, 15, " ", 1));
                        }
                        ESP_LOGI(TAG, "is_paused=%d", context->is_paused);
                    }
                    break;
                default:
                    ESP_LOGE(TAG, "Invalid cmd event");
                    break;
            }
        } else { // event timeout
            if (!context->show_menu) { // we are showing markets.
                if (!context->is_paused) {
                    market = market_search(market, true);
                }
                ESP_ERROR_CHECK(stat_print(context->disp_handle, market, false));
                if (market == NULL) { market = context->market_head; };
            }
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
    out_handle->is_enabled = true;
    out_handle->prev = NULL;
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
    ESP_ERROR_CHECK(market_new(&market_temp, "ETHUSDT"));
    context->market_head = market_temp;

    market_handle_t market_prev = context->market_head;
    for (uint8_t i = 0; i < 4; i++) {
        market_temp = NULL;
        ESP_ERROR_CHECK(market_new(&market_temp, MARKETS[i]));

        market_prev->next = market_temp;
        market_temp->prev = market_prev;
        market_prev = market_temp;
    }
    context->market_tail = market_temp;

    // make the liked list circular
    context->market_head->prev = context->market_tail;
    context->market_tail->next = context->market_head;

    context->selector = 0;
    context->show_menu = false;
    context->is_paused = false;

    ESP_LOGI(TAG, "Initialise GPIOs");
    gpio_config_t io_config = {
        .intr_type = GPIO_INTR_NEGEDGE, //interrupt of falling edge
        .mode = GPIO_MODE_INPUT, //set as input mode
        .pin_bit_mask = GPIO_INPUT_PIN_SEL, //bit mask of the pins to set
        .pull_down_en = 0, //disable pull-down mode
        .pull_up_en = 1, //enable pull-up mode
    };
    gpio_config(&io_config);

    //create a queue to handle cmd event from task
    context->cmd_event_queue = xQueueCreate(16, sizeof(cmd_event_t));
    ESP_RETURN_ON_FALSE(context->cmd_event_queue, ESP_ERR_NO_MEM, TAG, "no mem for cmd queue");
    //create a queue to handle key event from isr
    context->key_event_queue = xQueueCreate(16, sizeof(key_event_t));
    ESP_RETURN_ON_FALSE(context->key_event_queue, ESP_ERR_NO_MEM, TAG, "no mem for key queue");

    //install gpio isr service
    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT));
    //hook isr handler for specific gpio pin
    ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_INPUT_IO_0, gpio_isr_handler, (void*) context));
    ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_INPUT_IO_1, gpio_isr_handler, (void*) context));
    ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_INPUT_IO_2, gpio_isr_handler, (void*) context));
    ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_INPUT_IO_3, gpio_isr_handler, (void*) context));

    return ret;
}

void app_main(void)
{
    static app_context_t context = {0};

    ESP_LOGI(TAG, "Initialise App");
    ESP_ERROR_CHECK(app_init(&context));

    ESP_LOGI(TAG, "Create Tasks");
    xTaskCreate(gpio_get_task, "gpio_get_task", 2048, &context, 10, NULL);
    xTaskCreate(http_get_task, "http_get_task", 4096, &context, 20, NULL);
    xTaskCreate(disp_put_task, "disp_put_task", 4096, &context, 20, NULL);
}

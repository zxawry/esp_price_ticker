/*
 * file: app_ctrl.c
 * author: zxawry
 */

#include <stdio.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "app_disp.h"
#include "esp_check.h"
#include "app_comm.h"
#include "driver/gpio.h"

static const char *TAG = "app_ctrl";

#define OFFSET  (2) // which row on display to start printing markets
#define LENGTH  (4) // numbers of markets to display on each page

esp_err_t market_new(market_handle_t *market_handle, const char *symbol)
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

market_t *market_search(market_t *market, bool forward)
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

void stat_print(i2c_disp_handle_t disp_handle, market_t *market, bool first_run)
{
    ESP_ERROR_CHECK(i2c_disp_write(disp_handle, 0, 0, market->symbol, first_run ? 128 : 16));
    ESP_ERROR_CHECK(i2c_disp_write(disp_handle, 2, 0, *market->last_price ? market->last_price : "NaN", 16));
    ESP_ERROR_CHECK(i2c_disp_write(disp_handle, 4, 0, *market->last_update ? market->last_update : "NaN", 16));
}

void stat_empty(i2c_disp_handle_t disp_handle, bool first_run)
{
    ESP_ERROR_CHECK(i2c_disp_write(disp_handle, 0, 0, "No Valid Markets", first_run ? 128 : 16));
}

void stat_toggle(i2c_disp_handle_t disp_handle, bool is_paused)
{
    ESP_ERROR_CHECK(i2c_disp_write(disp_handle, 7, 15, is_paused ? "P" : " ", 1));
}

void menu_print(i2c_disp_handle_t disp_handle, market_t *market, uint8_t count, uint8_t selector, bool first_run)
{
    if (first_run) {
        ESP_ERROR_CHECK(i2c_disp_write(disp_handle, 0, 0, "Choose Markets:", 128));
    }

    // make sure market points to the first item of the page
    uint8_t start = selector;
    while (start % LENGTH != 0) {
        market = market->prev;
        start--;
    }

    for (uint8_t i = 0; i < LENGTH; i++) {
        if (i + start < count) {
            char status[5] = { '[', market->is_enabled ? '*' : ' ', ']', ' ', '\0' };
            ESP_ERROR_CHECK(i2c_disp_write(disp_handle, i + OFFSET, 0, status, 4));
            ESP_ERROR_CHECK(i2c_disp_write(disp_handle, i + OFFSET, 4, market->symbol, 12));
            market = market->next;
        } else {
            // draw blank line
            ESP_ERROR_CHECK(i2c_disp_write(disp_handle, i + OFFSET, 0, " ", 16));
        }
    }

    ESP_ERROR_CHECK(i2c_disp_highlight(disp_handle, true, (selector % LENGTH) + OFFSET, 0, 16));

    char line[16];
    uint8_t page = (count / LENGTH) + ((count % LENGTH) ? 1 : 0);
    snprintf(line, 16, "Page %d/%d", (selector / LENGTH) + 1, page);
    ESP_ERROR_CHECK(i2c_disp_write(disp_handle, 7, 0, line, 16));
}

void menu_backward(i2c_disp_handle_t disp_handle, market_t *market, uint8_t count, uint8_t selector)
{
    if ((selector + 1) % LENGTH == 0 || (selector + 1) == count) {
        // we need to draw a new page
        menu_print(disp_handle, market, count, selector, false);
    } else {
        uint8_t old_row = (selector + 1) % LENGTH;
        uint8_t new_row = selector % LENGTH;
        ESP_ERROR_CHECK(i2c_disp_highlight(disp_handle, false, old_row + OFFSET, 0, 16));
        ESP_ERROR_CHECK(i2c_disp_highlight(disp_handle, true, new_row + OFFSET, 0, 16));
    }
}

void menu_forward(i2c_disp_handle_t disp_handle, market_t *market, uint8_t count, uint8_t selector)
{
    if (selector % LENGTH == 0) {
        // we need to draw a new page
        menu_print(disp_handle, market, count, selector, false);
    } else {
        uint8_t old_row = (selector - 1) % LENGTH;
        uint8_t new_row = selector % LENGTH;
        ESP_ERROR_CHECK(i2c_disp_highlight(disp_handle, false, old_row + OFFSET, 0, 16));
        ESP_ERROR_CHECK(i2c_disp_highlight(disp_handle, true, new_row + OFFSET, 0, 16));
    }
}

void menu_toggle(i2c_disp_handle_t disp_handle, market_t *market, uint8_t selector)
{
    uint8_t row = selector % LENGTH;
    ESP_ERROR_CHECK(i2c_disp_write(disp_handle, row + OFFSET, 1, market->is_enabled ? "*" : " ", 1));
    ESP_ERROR_CHECK(i2c_disp_highlight(disp_handle, true, row + OFFSET, 1, 1));
}

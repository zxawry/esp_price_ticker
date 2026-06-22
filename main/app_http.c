/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <time.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"

#include "app_comm.h"

static const char *TAG = "app_http";

// root cert for the API server domain, taken from server_root_cert.pem
// extracted from the output of this command:
// `openssl s_client -showcerts -connect $DOMAIN_NAME:443`
// the root cert is the last cert given in the chain of certs.
extern const char server_root_cert_pem_start[] asm("_binary_server_root_cert_pem_start");
extern const char server_root_cert_pem_end[]   asm("_binary_server_root_cert_pem_end");

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *json_buffer;
    static int json_size;

    market_handle_t market_handle = (market_handle_t)evt->user_data;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (evt->data_len > 0) {
            if (json_buffer == NULL) {
                json_buffer = malloc(evt->data_len + 1);
            } else {
                json_buffer = realloc(json_buffer, json_size + evt->data_len + 1);
            }
            if (json_buffer == NULL) {
                ESP_LOGE(TAG, "Failed to allocate memory for data json_buffer");
                return ESP_FAIL;
            }
            memcpy(json_buffer + json_size, evt->data, evt->data_len);
            json_size += evt->data_len;
            json_buffer[json_size] = '\0';  // Null-terminate the string
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        if (json_buffer != NULL) {
            // start processing data
            cJSON *root = cJSON_Parse(json_buffer);
            cJSON *status = cJSON_GetObjectItem(root, "status");
            if (status == NULL || strcmp(status->valuestring, "ok") != 0) {
                ESP_LOGE(TAG, "Unexpected parsed status = %s", status->valuestring);
            } else { // status == "ok"
                cJSON *trades = cJSON_GetObjectItem(root, "trades");
                cJSON *latest_trade = cJSON_GetArrayItem(trades, 0);
                cJSON *price = cJSON_GetObjectItem(latest_trade, "price");
                cJSON *timestamp = cJSON_GetObjectItem(latest_trade, "time");

                // just to be more memory safe
                memset(market_handle->last_update, '\0', 16);
                memset(market_handle->last_price, '\0', 16);

                time_t seconds = (time_t) timestamp->valuedouble / 1000 + (35 * 360);
                strftime(market_handle->last_update, strlen("HH:MM:SS") + 1, "%H:%M:%S", gmtime(&seconds));

                memcpy(market_handle->last_price, price->valuestring, strlen(price->valuestring));
            }
            cJSON_Delete(root);
            free(json_buffer);
            json_buffer = NULL;
            json_size = 0;
        }
        break;
    default:
        break;
    }

    return ESP_OK;
}

esp_err_t http_request(market_handle_t market_handle)
{
    char url[128] = API_BASE_URL;
    strncat(url, market_handle->symbol, strlen(market_handle->symbol));

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 50000,
        .event_handler = _http_event_handler,
        .buffer_size = 2048,
        .user_data = market_handle,
        .cert_pem = server_root_cert_pem_start,
        .cert_len = server_root_cert_pem_end - server_root_cert_pem_start,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %lld",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);

    return err;
}

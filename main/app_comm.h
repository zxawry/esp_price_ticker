/*
 * file: app_comm.h
 * author: zxawry
 */

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

//extern const uint8_t ASCII_FONT[][8];

//extern esp_err_t wifi_init_sta(void);

//extern esp_err_t http_request(const char *url, char *buffer);

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

#define API_BASE_URL        "https://apiv2.nobitex.ir/v2/trades/"
#define API_BASE_URL_LEN    (strlen(API_BASE_URL))

struct market_t {
    char symbol[16];
    char last_price[16];
    char last_update[16];
    bool is_enabled;
    struct market_t *prev;
    struct market_t *next;
};

typedef struct market_t market_t;
typedef struct market_t *market_handle_t;

#ifdef __cplusplus
}
#endif

/*
 * file: app_comm.h
 * author: zxawry
 */

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

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

#define GPIO_KEY_MENU     (16) // switch between showing menu or markets stat
#define GPIO_KEY_PREV     (17) // select previous market in the menu or show pervious market stat
#define GPIO_KEY_NEXT     (18) // select next market in the menu or show pervious market stat
#define GPIO_KEY_FLOW     (19) // toggle enable flag of the selected market in the menu or pause iterating market stats
#define GPIO_KEYS_MASK    ((1ULL<<GPIO_KEY_MENU) | (1ULL<<GPIO_KEY_PREV) | (1ULL<<GPIO_KEY_NEXT) | (1ULL<<GPIO_KEY_FLOW))
#define ESP_INTR_FLAG_DEFAULT 0

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

typedef enum {
    KEY_MENU,
    KEY_PREV,
    KEY_NEXT,
    KEY_FLOW,
} key_event_t;

#ifdef __cplusplus
}
#endif

set(srcs "app_main.c" "app_font.c" "app_disp.c" "app_wifi.c" "app_http.c" "app_ctrl.c")

idf_component_register(SRCS ${srcs}
                    PRIV_REQUIRES esp_driver_i2c esp_wifi nvs_flash esp_http_client json esp_driver_gpio
                    EMBED_TXTFILES server_root_cert.pem
                    INCLUDE_DIRS ".")

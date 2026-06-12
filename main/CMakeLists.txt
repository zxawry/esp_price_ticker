set(srcs "app_main.c" "app_font.c" "app_disp.c")

idf_component_register(SRCS ${srcs}
                    PRIV_REQUIRES esp_driver_i2c
                    INCLUDE_DIRS ".")

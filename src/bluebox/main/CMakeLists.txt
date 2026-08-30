idf_component_register(SRCS "bluetooth.c" "encoder.c" "main.c" "switch.c"
                    REQUIRES bt nvs_flash esp_driver_i2s esp_driver_gpio esp_ringbuf
                    INCLUDE_DIRS ".")
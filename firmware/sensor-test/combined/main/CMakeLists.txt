idf_component_register(SRCS "main.c" "scd41.c" "pms5003.c" "network.c" "hx711.c"
    INCLUDE_DIRS "." PRIV_REQUIRES esp_driver_i2c esp_driver_uart esp_driver_gpio esp_rom
    esp_event esp_http_client esp_netif esp_wifi nvs_flash)

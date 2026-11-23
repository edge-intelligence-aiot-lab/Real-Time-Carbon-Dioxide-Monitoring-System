idf_component_register(SRCS "main.c" "scd41.c" "pms5003.c" "network.c"
    INCLUDE_DIRS "." PRIV_REQUIRES esp_driver_i2c esp_driver_uart
    esp_event esp_http_client esp_netif esp_wifi nvs_flash)

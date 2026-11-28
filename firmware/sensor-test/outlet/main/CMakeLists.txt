# Reuse the sensor readers from the combined project.
idf_component_register(
    SRCS "main.c" "../../combined/main/scd41.c" "../../combined/main/pms5003.c"
    INCLUDE_DIRS "../../combined/main"
    PRIV_REQUIRES esp_driver_i2c esp_driver_uart)

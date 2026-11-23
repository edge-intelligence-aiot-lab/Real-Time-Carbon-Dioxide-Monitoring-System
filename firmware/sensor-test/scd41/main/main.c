#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "scd41";

static uint16_t read_word(const uint8_t *data)
{
    return ((uint16_t)data[0] << 8) | data[1];
}

static uint8_t crc8(const uint8_t *data)
{
    uint8_t crc = 0xff;
    for (int i = 0; i < 2; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static esp_err_t send_command(i2c_master_dev_handle_t sensor, uint16_t command)
{
    uint8_t bytes[] = {command >> 8, command & 0xff};
    return i2c_master_transmit(sensor, bytes, sizeof(bytes), 1000);
}

static esp_err_t read_response(i2c_master_dev_handle_t sensor, uint16_t command,
                               uint8_t *data, size_t length)
{
    esp_err_t result = send_command(sensor, command);
    if (result != ESP_OK) {
        return result;
    }
    // Separate write/read transactions and allow command execution to finish.
    vTaskDelay(pdMS_TO_TICKS(10));
    result = i2c_master_receive(sensor, data, length, 1000);
    if (result != ESP_OK) {
        return result;
    }
    for (size_t i = 0; i < length; i += 3) {
        if (crc8(data + i) != data[i + 2]) {
            return ESP_ERR_INVALID_CRC;
        }
    }
    return ESP_OK;
}

void app_main(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = 21,
        .scl_io_num = 22,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus));
    i2c_device_config_t sensor_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x62,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t sensor;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &sensor_config, &sensor));

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_err_t result = i2c_master_probe(bus, 0x62, 1000);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Sensor not found at 0x62. Check 3.3V, GND, SDA=G21, SCL=G22 and module pull-ups.");
        return;
    }
    // Stop any measurement left running by an earlier ESP32 reset.
    result = send_command(sensor, 0x3f86);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Stop command failed: %s", esp_err_to_name(result));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    result = send_command(sensor, 0x21b1);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Start command failed: %s", esp_err_to_name(result));
        return;
    }
    ESP_LOGI(TAG, "SCD41 started. First reading takes approximately 5 seconds.");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint8_t ready[3];
        result = read_response(sensor, 0xe4b8, ready, sizeof(ready));
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "Data-ready check failed: %s", esp_err_to_name(result));
            continue;
        }
        if ((read_word(ready) & 0x07ff) == 0) {
            continue;
        }
        uint8_t data[9];
        result = read_response(sensor, 0xec05, data, sizeof(data));
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "Reading failed: %s", esp_err_to_name(result));
            continue;
        }
        uint16_t co2 = read_word(data);
        if (co2 == 0) {
            ESP_LOGW(TAG, "Sensor returned an invalid zero CO2 reading; waiting for next sample.");
            continue;
        }
        float temperature = -45.0f + 175.0f * read_word(data + 3) / 65535.0f;
        float humidity = 100.0f * read_word(data + 6) / 65535.0f;
        ESP_LOGI(TAG, "CO2: %u ppm | Temperature: %.2f C | Humidity: %.2f %%RH",
                 (unsigned)co2, temperature, humidity);
    }
}

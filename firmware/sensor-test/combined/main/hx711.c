#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define HX711_DT GPIO_NUM_32
#define HX711_SCK GPIO_NUM_33

static const char *TAG = "hx711";
static portMUX_TYPE clock_lock = portMUX_INITIALIZER_UNLOCKED;

static bool read_raw(int32_t *value)
{
    TickType_t start = xTaskGetTickCount();
    while (gpio_get_level(HX711_DT) != 0) {
        if (xTaskGetTickCount() - start >= pdMS_TO_TICKS(1000)) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    uint32_t bits = 0;
    // Keep clock-high pulses short. A high pulse over 60 us powers down the HX711.
    portENTER_CRITICAL(&clock_lock);
    for (int bit = 0; bit < 24; bit++) {
        gpio_set_level(HX711_SCK, 1);
        esp_rom_delay_us(1);
        bits = (bits << 1) | gpio_get_level(HX711_DT);
        gpio_set_level(HX711_SCK, 0);
        esp_rom_delay_us(1);
    }
    // Pulse 25 selects channel A with gain 128 for the next conversion.
    gpio_set_level(HX711_SCK, 1);
    esp_rom_delay_us(1);
    gpio_set_level(HX711_SCK, 0);
    esp_rom_delay_us(1);
    portEXIT_CRITICAL(&clock_lock);

    *value = (int32_t)bits;
    if (bits & 0x800000) {
        *value -= 0x1000000;
    }
    return true;
}

void hx711_task(void *argument)
{
    gpio_config_t clock = {
        .pin_bit_mask = 1ULL << HX711_SCK,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&clock));
    ESP_ERROR_CHECK(gpio_set_level(HX711_SCK, 0));
    gpio_config_t data = {
        .pin_bit_mask = 1ULL << HX711_DT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&data));
    ESP_LOGI(TAG, "Raw test started: DT=32, SCK=33. Calibration to grams is pending.");

    while (true) {
        int32_t raw;
        if (!read_raw(&raw)) {
            ESP_LOGW(TAG, "No ready signal. Check 3V3, GND, DT=G32 and SCK=G33.");
        } else if (raw == 8388607 || raw == -8388608) {
            ESP_LOGW(TAG, "ADC saturated (%" PRId32 "). Check bridge wiring, mounting and load.", raw);
        } else {
            ESP_LOGI(TAG, "Raw load-cell reading: %" PRId32 " counts", raw);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

#include <stdint.h>
#include "sensor_data.h"
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PMS_UART UART_NUM_2
#define PMS_RX_PIN 16
#define FRAME_SIZE 32

static const char *TAG = "pms5003";

static uint16_t read_u16(const uint8_t *bytes)
{
    return ((uint16_t)bytes[0] << 8) | bytes[1];
}

static void print_frame(const uint8_t *frame)
{
    pms5003_sample_t sample = {
        .pm25_ug_m3 = read_u16(frame + 12),
        .received_at = xTaskGetTickCount(),
    };
    xQueueOverwrite(pms5003_samples, &sample);
    // Atmospheric-environment values, distinct from the CF=1 factory values.
    ESP_LOGI(TAG, "PM1.0: %u | PM2.5: %u | PM10: %u ug/m3",
             (unsigned)read_u16(frame + 10),
             (unsigned)read_u16(frame + 12),
             (unsigned)read_u16(frame + 14));
}

void pms5003_task(void *argument)
{
    const uart_config_t config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(PMS_UART, 1024, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(PMS_UART, &config));
    ESP_ERROR_CHECK(uart_set_pin(PMS_UART, UART_PIN_NO_CHANGE, PMS_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "Listening at 9600 baud on GPIO16. Allow at least 30 seconds for warm-up.");

    uint8_t frame[FRAME_SIZE];
    size_t used = 0;
    while (true) {
        uint8_t byte;
        int count = uart_read_bytes(PMS_UART, &byte, 1, pdMS_TO_TICKS(5000));
        if (count <= 0) {
            used = 0;
            ESP_LOGW(TAG, "No serial data. Check sensor 5V, common GND, and TXD to GPIO16.");
            continue;
        }
        frame[used++] = byte;
        // Keep a sliding window so partial reads and corrupt frames resynchronize.
        while (used >= 2 && (frame[0] != 0x42 || frame[1] != 0x4d)) {
            memmove(frame, frame + 1, --used);
        }
        if (used < FRAME_SIZE) {
            continue;
        }
        uint16_t checksum = 0;
        for (size_t i = 0; i < FRAME_SIZE - 2; i++) {
            checksum += frame[i];
        }
        if (read_u16(frame + 2) == 28 && checksum == read_u16(frame + 30)) {
            print_frame(frame);
            used = 0;
        } else {
            ESP_LOGW(TAG, "Discarding an invalid sensor frame");
            memmove(frame, frame + 1, FRAME_SIZE - 1);
            used = FRAME_SIZE - 1;
        }
    }
}

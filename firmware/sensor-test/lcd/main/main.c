#include <stdint.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LCD_DC GPIO_NUM_26
#define LCD_RESET GPIO_NUM_25
static spi_device_handle_t lcd;

static void transfer(const void *data, size_t bytes, int is_data)
{
    ESP_ERROR_CHECK(gpio_set_level(LCD_DC, is_data));
    spi_transaction_t transaction = {
        .length = bytes * 8,
        .tx_buffer = data,
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(lcd, &transaction));
}

static void command(uint8_t code, const uint8_t *data, size_t length)
{
    transfer(&code, 1, 0);
    if (length > 0) {
        transfer(data, length, 1);
    }
}

static void read_register(uint8_t address)
{
    uint8_t response[4] = {0};
    ESP_ERROR_CHECK(spi_device_acquire_bus(lcd, portMAX_DELAY));
    ESP_ERROR_CHECK(gpio_set_level(LCD_DC, 0));
    spi_transaction_t request = {
        .length = 8,
        .tx_buffer = &address,
        .flags = SPI_TRANS_CS_KEEP_ACTIVE,
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(lcd, &request));
    ESP_ERROR_CHECK(gpio_set_level(LCD_DC, 1));
    spi_transaction_t read = {
        .rxlength = sizeof(response) * 8,
        .rx_buffer = response,
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(lcd, &read));
    spi_device_release_bus(lcd);
    ESP_LOGI("lcd_id", "Register 0x%02X: %02X %02X %02X %02X (first byte may be dummy)",
             address, response[0], response[1], response[2], response[3]);
}

static void rectangle(int x, int y, int width, int height, uint16_t color)
{
    uint8_t columns[] = {x >> 8, x & 255, (x + width - 1) >> 8, (x + width - 1) & 255};
    uint8_t rows[] = {y >> 8, y & 255, (y + height - 1) >> 8, (y + height - 1) & 255};
    command(0x2a, columns, sizeof(columns));
    command(0x2b, rows, sizeof(rows));
    command(0x2c, NULL, 0);
    uint8_t pixels[640];
    for (int i = 0; i < width; i++) {
        pixels[2 * i] = color >> 8;
        pixels[2 * i + 1] = color & 255;
    }
    for (int row = 0; row < height; row++) {
        transfer(pixels, width * 2, 1);
    }
}

// Five columns per glyph, least significant bit at the top.
static const char letters[] = "HELLOTFT OK";
static const uint8_t glyphs[][5] = {
    {0x7f,0x08,0x08,0x08,0x7f}, // H
    {0x7f,0x49,0x49,0x49,0x41}, // E
    {0x7f,0x40,0x40,0x40,0x40}, // L
    {0x7f,0x40,0x40,0x40,0x40}, // L
    {0x3e,0x41,0x41,0x41,0x3e}, // O
    {0x01,0x01,0x7f,0x01,0x01}, // T
    {0x7f,0x09,0x09,0x09,0x01}, // F
    {0x01,0x01,0x7f,0x01,0x01}, // T
    {0,0,0,0,0},
    {0x3e,0x41,0x41,0x41,0x3e}, // O
    {0x7f,0x08,0x14,0x22,0x41}, // K
};

static void text(int x, int y, const char *message)
{
    for (const char *p = message; *p; p++, x += 24) {
        const char *match = strchr(letters, *p);
        if (!match) {
            continue;
        }
        const uint8_t *glyph = glyphs[match - letters];
        for (int column = 0; column < 5; column++) {
            for (int row = 0; row < 7; row++) {
                if (glyph[column] & (1 << row)) {
                    rectangle(x + column * 4, y + row * 4, 4, 4, 0xffff);
                }
            }
        }
    }
}

void app_main(void)
{
    gpio_config_t pins = {
        .pin_bit_mask = (1ULL << LCD_DC) | (1ULL << LCD_RESET),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&pins));
    spi_bus_config_t bus = {
        .mosi_io_num = 23,
        .miso_io_num = 19,
        .sclk_io_num = 18,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 640,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    spi_device_interface_config_t device = {
        .clock_speed_hz = 1000000,
        .mode = 0,
        .spics_io_num = 27,
        .queue_size = 1,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &device, &lcd));
    ESP_ERROR_CHECK(gpio_set_level(LCD_RESET, 0));
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_ERROR_CHECK(gpio_set_level(LCD_RESET, 1));
    vTaskDelay(pdMS_TO_TICKS(150));
    command(0x01, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(150));
    read_register(0xd3);
    read_register(0x04);
    const uint8_t power_b[] = {0x00,0xc1,0x30};
    command(0xcf, power_b, sizeof(power_b));
    const uint8_t power_sequence[] = {0x64,0x03,0x12,0x81};
    command(0xed, power_sequence, sizeof(power_sequence));
    const uint8_t timing[] = {0x85,0x00,0x78};
    command(0xe8, timing, sizeof(timing));
    const uint8_t power_a[] = {0x39,0x2c,0x00,0x34,0x02};
    command(0xcb, power_a, sizeof(power_a));
    const uint8_t pump = 0x20;
    command(0xf7, &pump, 1);
    const uint8_t power1 = 0x23, power2 = 0x10;
    command(0xc0, &power1, 1);
    command(0xc1, &power2, 1);
    const uint8_t vcom[] = {0x3e,0x28};
    command(0xc5, vcom, sizeof(vcom));
    const uint8_t vcom2 = 0x86;
    command(0xc7, &vcom2, 1);
    const uint8_t orientation = 0x88, format = 0x55;
    command(0x36, &orientation, 1);
    command(0x3a, &format, 1);
    const uint8_t frame_rate[] = {0x00,0x18};
    command(0xb1, frame_rate, sizeof(frame_rate));
    const uint8_t display[] = {0x08,0x82,0x27};
    command(0xb6, display, sizeof(display));
    command(0x11, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(150));
    command(0x29, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    const uint16_t colors[] = {0xf800,0x07e0,0x001f};
    for (int i = 0; i < 3; i++) {
        rectangle(0, 0, 320, 240, colors[i]);
        vTaskDelay(pdMS_TO_TICKS(700));
    }
    rectangle(0, 0, 320, 240, 0x1084);
    text(100, 40, "HELLO");
    text(88, 95, "TFT OK");
    rectangle(30, 155, 70, 55, 0xf800);
    rectangle(125, 155, 70, 55, 0x07e0);
    rectangle(220, 155, 70, 55, 0x001f);
    rectangle(0, 0, 320, 3, 0xffff);
    rectangle(0, 237, 320, 3, 0xffff);
    rectangle(0, 0, 3, 240, 0xffff);
    rectangle(317, 0, 3, 240, 0xffff);
    ESP_LOGI("lcd_test", "Display commands sent. Expect HELLO, TFT OK, and red/green/blue blocks.");
}

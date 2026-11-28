#include "esp_err.h"
#include "sensor_data.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

QueueHandle_t scd41_samples;
QueueHandle_t pms5003_samples;

void scd41_task(void *argument);
void pms5003_task(void *argument);

void app_main(void)
{
    scd41_samples = xQueueCreate(1, sizeof(scd41_sample_t));
    pms5003_samples = xQueueCreate(1, sizeof(pms5003_sample_t));
    ESP_ERROR_CHECK(scd41_samples && pms5003_samples ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_LOGI("sensor_test", "Outlet test: starting SCD41 (SDA=21, SCL=22) and PMS5003 (RX=16)");
    // Independent tasks keep UART reception running during SCD41 waits.
    BaseType_t result = xTaskCreate(scd41_task, "scd41", 4096, NULL, 5, NULL);
    ESP_ERROR_CHECK(result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    result = xTaskCreate(pms5003_task, "pms5003", 4096, NULL, 5, NULL);
    ESP_ERROR_CHECK(result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

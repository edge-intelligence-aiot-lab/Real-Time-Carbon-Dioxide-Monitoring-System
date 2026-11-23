#include <stdio.h>
#include "sensor_data.h"
#include <string.h>

#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "co2_node";
static const EventBits_t WIFI_READY = BIT0;
static EventGroupHandle_t wifi_events;

static void handle_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && (id == WIFI_EVENT_STA_START || id == WIFI_EVENT_STA_DISCONNECTED)) {
        xEventGroupClearBits(wifi_events, WIFI_READY);
        esp_err_t result = esp_wifi_connect();
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "Wi-Fi connection attempt failed: %s", esp_err_to_name(result));
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_events, WIFI_READY);
    }
}

static void start_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_sta() ? ESP_OK : ESP_FAIL);

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, handle_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, handle_wifi_event, NULL));

    wifi_config_t config = {0};
    memcpy(config.sta.ssid, CONFIG_CO2_WIFI_SSID, strlen(CONFIG_CO2_WIFI_SSID));
    memcpy(config.sta.password, CONFIG_CO2_WIFI_PASSWORD, strlen(CONFIG_CO2_WIFI_PASSWORD));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static bool send_reading(const scd41_sample_t *scd, const pms5003_sample_t *pm)
{
    char payload[256];
    char particulate[64] = "";
    if (pm != NULL) {
        snprintf(particulate, sizeof(particulate), ",\"inlet_particulate_ug_m3\":%u",
                 (unsigned)pm->pm25_ug_m3);
    }
    int length = snprintf(payload, sizeof(payload),
        "{\"co2_ppm\":%u,\"inlet_temperature_c\":%.3f,\"inlet_humidity_rh\":%.3f%s}",
        (unsigned)scd->co2_ppm, scd->temperature_c, scd->humidity_rh, particulate);
    if (length < 0 || length >= sizeof(payload)) {
        ESP_LOGE(TAG, "Sensor payload exceeds buffer");
        return false;
    }
    esp_http_client_config_t config = {
        .url = CONFIG_CO2_SERVER_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 4000,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGW(TAG, "Could not initialize the HTTP client");
        return false;
    }
    bool saved = false;
    esp_err_t result = esp_http_client_set_header(client, "Content-Type", "application/json");
    if (result == ESP_OK) {
        result = esp_http_client_set_post_field(client, payload, length);
    }
    if (result == ESP_OK) {
        result = esp_http_client_perform(client);
    }
    if (result == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 201) {
            saved = true;
            ESP_LOGI(TAG, "Saved inlet sensor reading (HTTP 201)");
        } else {
            ESP_LOGW(TAG, "Server returned HTTP %d", status);
        }
    } else {
        ESP_LOGW(TAG, "Could not send sample: %s", esp_err_to_name(result));
    }
    esp_http_client_cleanup(client);
    return saved;
}

void network_task(void *argument)
{
    if (strlen(CONFIG_CO2_WIFI_SSID) == 0 || strlen(CONFIG_CO2_WIFI_SSID) > 32 ||
        strlen(CONFIG_CO2_WIFI_PASSWORD) > 64 ||
        strncmp(CONFIG_CO2_SERVER_URL, "http://", 7) != 0) {
        ESP_LOGE(TAG, "Configure Wi-Fi and the local HTTP URL in CO2 monitor node settings");
        vTaskDelete(NULL);
        return;
    }

    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);

    wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(wifi_events ? ESP_OK : ESP_ERR_NO_MEM);
    start_wifi();

    bool sent_before = false;
    TickType_t last_sent = 0;
    TickType_t started_at = xTaskGetTickCount();
    while (true) {
        EventBits_t bits = xEventGroupWaitBits(wifi_events, WIFI_READY, pdFALSE, pdTRUE,
                                               pdMS_TO_TICKS(5000));
        scd41_sample_t scd;
        pms5003_sample_t pm;
        TickType_t now = xTaskGetTickCount();
        if ((bits & WIFI_READY) && xQueuePeek(scd41_samples, &scd, 0) == pdTRUE &&
            now - scd.received_at <= pdMS_TO_TICKS(15000) &&
            (!sent_before || scd.received_at != last_sent)) {
            bool pm_ready = xQueuePeek(pms5003_samples, &pm, 0) == pdTRUE &&
                now - pm.received_at <= pdMS_TO_TICKS(10000) &&
                now - started_at >= pdMS_TO_TICKS(30000);
            if (send_reading(&scd, pm_ready ? &pm : NULL)) {
                last_sent = scd.received_at;
                sent_before = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

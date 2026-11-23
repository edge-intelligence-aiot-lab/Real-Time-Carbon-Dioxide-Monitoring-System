#pragma once
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef struct {
    uint16_t co2_ppm;
    float temperature_c;
    float humidity_rh;
    TickType_t received_at;
} scd41_sample_t;

typedef struct {
    uint16_t pm25_ug_m3;
    TickType_t received_at;
} pms5003_sample_t;

extern QueueHandle_t scd41_samples;
extern QueueHandle_t pms5003_samples;

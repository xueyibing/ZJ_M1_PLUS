#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define DATA_MANAGER_STRESS_ITEM_COUNT 6U
#define DATA_MANAGER_STRESS_DEFAULT_TEST_COUNT 5000
#define DATA_MANAGER_STRESS_DEFAULT_INTERVAL_S 60

typedef struct {
    int32_t test_count;
    int32_t interval_s;
    int32_t executed_count;
} data_manager_stress_record_t;

typedef enum {
    DATA_MANAGER_LANGUAGE_CHINESE = 0,
    DATA_MANAGER_LANGUAGE_ENGLISH = 1,
} data_manager_language_t;

esp_err_t data_manager_init(void);
void data_manager_get_stress_record(size_t index, data_manager_stress_record_t *record);
esp_err_t data_manager_set_stress_test_count(size_t index, int32_t test_count);
esp_err_t data_manager_set_stress_interval(size_t index, int32_t interval_s);
esp_err_t data_manager_set_stress_executed_count(size_t index, int32_t executed_count, bool force_flush);
bool data_manager_get_auto_clean_enabled(void);
esp_err_t data_manager_set_auto_clean_enabled(bool enabled);
data_manager_language_t data_manager_get_language(void);
esp_err_t data_manager_set_language(data_manager_language_t language);
uint8_t data_manager_get_brightness(void);
esp_err_t data_manager_set_brightness(uint8_t brightness);
bool data_manager_get_last_reservation_time(uint8_t *hour, uint8_t *minute);
esp_err_t data_manager_set_last_reservation_time(uint8_t hour, uint8_t minute);
esp_err_t data_manager_flush(void);

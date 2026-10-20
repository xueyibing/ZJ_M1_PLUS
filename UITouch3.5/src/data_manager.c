#include "data_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

#define DATA_MANAGER_NAMESPACE "stress"
#define DATA_MANAGER_EXECUTED_FLUSH_STEP 10
#define DATA_MANAGER_EXECUTED_FLUSH_INTERVAL_MS 30000ULL
#define DATA_MANAGER_BRIGHTNESS_DEFAULT 100
#define DATA_MANAGER_BRIGHTNESS_MIN 20
#define DATA_MANAGER_BRIGHTNESS_MAX 100
#define DATA_MANAGER_RESERVATION_MINUTES_INVALID (-1)
#define DATA_MANAGER_MINUTES_PER_DAY (24 * 60)

static const char *TAG = "data_manager";

typedef struct {
    bool initialized;
    nvs_handle_t nvs_handle;
    data_manager_stress_record_t records[DATA_MANAGER_STRESS_ITEM_COUNT];
    int32_t last_flushed_executed[DATA_MANAGER_STRESS_ITEM_COUNT];
    bool executed_dirty[DATA_MANAGER_STRESS_ITEM_COUNT];
    uint64_t last_executed_flush_ms;
    bool auto_clean_enabled;
    int32_t language;
    uint8_t brightness;
    int32_t last_reservation_minutes;
} data_manager_ctx_t;

static data_manager_ctx_t s_data_manager;

static data_manager_ctx_t *data_manager_instance(void)
{
    return &s_data_manager;
}

static bool data_manager_is_valid_index(size_t index)
{
    return index < DATA_MANAGER_STRESS_ITEM_COUNT;
}

static uint64_t data_manager_now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}

static void data_manager_make_key(char prefix, size_t index, char *buffer, size_t buffer_size)
{
    if(buffer == NULL || buffer_size == 0U) {
        return;
    }

    (void)snprintf(buffer, buffer_size, "%c%u", prefix, (unsigned)index);
}

static esp_err_t data_manager_read_i32(const char *key, int32_t default_value, int32_t *out_value)
{
    data_manager_ctx_t *ctx = data_manager_instance();
    esp_err_t err;
    int32_t value = default_value;

    if(key == NULL || out_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_get_i32(ctx->nvs_handle, key, &value);
    if(err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
        value = default_value;
    }
    if(err != ESP_OK) {
        return err;
    }

    *out_value = value;
    return ESP_OK;
}

static esp_err_t data_manager_write_i32(const char *key, int32_t value)
{
    data_manager_ctx_t *ctx = data_manager_instance();

    if(key == NULL || !ctx->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    return nvs_set_i32(ctx->nvs_handle, key, value);
}

static esp_err_t data_manager_flush_executed_index(size_t index)
{
    data_manager_ctx_t *ctx = data_manager_instance();
    char key[8];
    esp_err_t err;

    if(!data_manager_is_valid_index(index) || !ctx->executed_dirty[index]) {
        return ESP_OK;
    }

    data_manager_make_key('e', index, key, sizeof(key));
    err = data_manager_write_i32(key, ctx->records[index].executed_count);
    if(err != ESP_OK) {
        return err;
    }

    ctx->last_flushed_executed[index] = ctx->records[index].executed_count;
    ctx->executed_dirty[index] = false;
    return ESP_OK;
}

esp_err_t data_manager_init(void)
{
    data_manager_ctx_t *ctx = data_manager_instance();
    esp_err_t err;
    size_t index;

    if(ctx->initialized) {
        return ESP_OK;
    }

    memset(ctx, 0, sizeof(*ctx));

    err = nvs_open(DATA_MANAGER_NAMESPACE, NVS_READWRITE, &ctx->nvs_handle);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    for(index = 0; index < DATA_MANAGER_STRESS_ITEM_COUNT; index++) {
        char key[8];

        data_manager_make_key('t', index, key, sizeof(key));
        err = data_manager_read_i32(key,
                                    DATA_MANAGER_STRESS_DEFAULT_TEST_COUNT,
                                    &ctx->records[index].test_count);
        if(err != ESP_OK) {
            return err;
        }

        data_manager_make_key('i', index, key, sizeof(key));
        err = data_manager_read_i32(key,
                                    DATA_MANAGER_STRESS_DEFAULT_INTERVAL_S,
                                    &ctx->records[index].interval_s);
        if(err != ESP_OK) {
            return err;
        }

        data_manager_make_key('e', index, key, sizeof(key));
        err = data_manager_read_i32(key, 0, &ctx->records[index].executed_count);
        if(err != ESP_OK) {
            return err;
        }

        ctx->last_flushed_executed[index] = ctx->records[index].executed_count;
    }

    {
        int32_t auto_clean = 0;
        err = data_manager_read_i32("auto_clean", 0, &auto_clean);
        if(err != ESP_OK) {
            return err;
        }
        ctx->auto_clean_enabled = auto_clean != 0;
    }

    {
        int32_t language = DATA_MANAGER_LANGUAGE_CHINESE;
        err = data_manager_read_i32("language", DATA_MANAGER_LANGUAGE_CHINESE, &language);
        if(err != ESP_OK) {
            return err;
        }
        ctx->language = (language == DATA_MANAGER_LANGUAGE_ENGLISH)
                            ? DATA_MANAGER_LANGUAGE_ENGLISH
                            : DATA_MANAGER_LANGUAGE_CHINESE;
    }

    {
        int32_t brightness = DATA_MANAGER_BRIGHTNESS_DEFAULT;
        err = data_manager_read_i32("brightness",
                                    DATA_MANAGER_BRIGHTNESS_DEFAULT,
                                    &brightness);
        if(err != ESP_OK) {
            return err;
        }
        if(brightness < DATA_MANAGER_BRIGHTNESS_MIN ||
           brightness > DATA_MANAGER_BRIGHTNESS_MAX) {
            brightness = DATA_MANAGER_BRIGHTNESS_DEFAULT;
        }
        ctx->brightness = (uint8_t)brightness;
    }

    {
        int32_t reservation_minutes = DATA_MANAGER_RESERVATION_MINUTES_INVALID;
        err = data_manager_read_i32("reserve_min",
                                    DATA_MANAGER_RESERVATION_MINUTES_INVALID,
                                    &reservation_minutes);
        if(err != ESP_OK) {
            return err;
        }
        if(reservation_minutes < 0 ||
           reservation_minutes >= DATA_MANAGER_MINUTES_PER_DAY ||
           reservation_minutes % 30 != 0) {
            reservation_minutes = DATA_MANAGER_RESERVATION_MINUTES_INVALID;
        }
        ctx->last_reservation_minutes = reservation_minutes;
    }

    ctx->last_executed_flush_ms = data_manager_now_ms();
    ctx->initialized = true;
    return ESP_OK;
}

void data_manager_get_stress_record(size_t index, data_manager_stress_record_t *record)
{
    data_manager_ctx_t *ctx = data_manager_instance();

    if(record == NULL) {
        return;
    }

    memset(record, 0, sizeof(*record));
    if(!ctx->initialized || !data_manager_is_valid_index(index)) {
        return;
    }

    *record = ctx->records[index];
}

esp_err_t data_manager_set_stress_test_count(size_t index, int32_t test_count)
{
    data_manager_ctx_t *ctx = data_manager_instance();
    char key[8];
    esp_err_t err;

    if(!ctx->initialized || !data_manager_is_valid_index(index)) {
        return ESP_ERR_INVALID_ARG;
    }

    ctx->records[index].test_count = test_count;
    data_manager_make_key('t', index, key, sizeof(key));
    err = data_manager_write_i32(key, test_count);
    if(err != ESP_OK) {
        return err;
    }

    return nvs_commit(ctx->nvs_handle);
}

esp_err_t data_manager_set_stress_interval(size_t index, int32_t interval_s)
{
    data_manager_ctx_t *ctx = data_manager_instance();
    char key[8];
    esp_err_t err;

    if(!ctx->initialized || !data_manager_is_valid_index(index)) {
        return ESP_ERR_INVALID_ARG;
    }

    ctx->records[index].interval_s = interval_s;
    data_manager_make_key('i', index, key, sizeof(key));
    err = data_manager_write_i32(key, interval_s);
    if(err != ESP_OK) {
        return err;
    }

    return nvs_commit(ctx->nvs_handle);
}

esp_err_t data_manager_set_stress_executed_count(size_t index, int32_t executed_count, bool force_flush)
{
    data_manager_ctx_t *ctx = data_manager_instance();
    uint64_t now_ms;

    if(!ctx->initialized || !data_manager_is_valid_index(index)) {
        return ESP_ERR_INVALID_ARG;
    }

    ctx->records[index].executed_count = executed_count;
    ctx->executed_dirty[index] = true;
    now_ms = data_manager_now_ms();

    if(force_flush ||
       executed_count < ctx->last_flushed_executed[index] ||
       abs(executed_count - ctx->last_flushed_executed[index]) >= DATA_MANAGER_EXECUTED_FLUSH_STEP ||
       now_ms - ctx->last_executed_flush_ms >= DATA_MANAGER_EXECUTED_FLUSH_INTERVAL_MS) {
        return data_manager_flush();
    }

    return ESP_OK;
}

bool data_manager_get_auto_clean_enabled(void)
{
    data_manager_ctx_t *ctx = data_manager_instance();

    return ctx->initialized && ctx->auto_clean_enabled;
}

esp_err_t data_manager_set_auto_clean_enabled(bool enabled)
{
    data_manager_ctx_t *ctx = data_manager_instance();
    esp_err_t err;

    if(!ctx->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    err = data_manager_write_i32("auto_clean", enabled ? 1 : 0);
    if(err != ESP_OK) {
        return err;
    }

    err = nvs_commit(ctx->nvs_handle);
    if(err == ESP_OK) {
        ctx->auto_clean_enabled = enabled;
    }
    return err;
}

data_manager_language_t data_manager_get_language(void)
{
    data_manager_ctx_t *ctx = data_manager_instance();

    if(!ctx->initialized || ctx->language != DATA_MANAGER_LANGUAGE_ENGLISH) {
        return DATA_MANAGER_LANGUAGE_CHINESE;
    }
    return DATA_MANAGER_LANGUAGE_ENGLISH;
}

esp_err_t data_manager_set_language(data_manager_language_t language)
{
    data_manager_ctx_t *ctx = data_manager_instance();
    esp_err_t err;
    int32_t value = (language == DATA_MANAGER_LANGUAGE_ENGLISH)
                        ? DATA_MANAGER_LANGUAGE_ENGLISH
                        : DATA_MANAGER_LANGUAGE_CHINESE;

    if(!ctx->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    err = data_manager_write_i32("language", value);
    if(err != ESP_OK) {
        return err;
    }

    err = nvs_commit(ctx->nvs_handle);
    if(err == ESP_OK) {
        ctx->language = value;
    }
    return err;
}

uint8_t data_manager_get_brightness(void)
{
    data_manager_ctx_t *ctx = data_manager_instance();

    return ctx->initialized ? ctx->brightness : DATA_MANAGER_BRIGHTNESS_DEFAULT;
}

esp_err_t data_manager_set_brightness(uint8_t brightness)
{
    data_manager_ctx_t *ctx = data_manager_instance();
    esp_err_t err;

    if(!ctx->initialized || brightness < DATA_MANAGER_BRIGHTNESS_MIN ||
       brightness > DATA_MANAGER_BRIGHTNESS_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    err = data_manager_write_i32("brightness", brightness);
    if(err != ESP_OK) {
        return err;
    }

    err = nvs_commit(ctx->nvs_handle);
    if(err == ESP_OK) {
        ctx->brightness = brightness;
    }
    return err;
}

bool data_manager_get_last_reservation_time(uint8_t *hour, uint8_t *minute)
{
    data_manager_ctx_t *ctx = data_manager_instance();

    if(hour == NULL || minute == NULL || !ctx->initialized ||
       ctx->last_reservation_minutes < 0 ||
       ctx->last_reservation_minutes >= DATA_MANAGER_MINUTES_PER_DAY) {
        return false;
    }

    *hour = (uint8_t)(ctx->last_reservation_minutes / 60);
    *minute = (uint8_t)(ctx->last_reservation_minutes % 60);
    return true;
}

esp_err_t data_manager_set_last_reservation_time(uint8_t hour, uint8_t minute)
{
    data_manager_ctx_t *ctx = data_manager_instance();
    int32_t reservation_minutes;
    esp_err_t err;

    if(!ctx->initialized || hour >= 24U || minute >= 60U || minute % 30U != 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    reservation_minutes = (int32_t)hour * 60 + minute;
    err = data_manager_write_i32("reserve_min", reservation_minutes);
    if(err != ESP_OK) {
        return err;
    }

    err = nvs_commit(ctx->nvs_handle);
    if(err == ESP_OK) {
        ctx->last_reservation_minutes = reservation_minutes;
    }
    return err;
}

esp_err_t data_manager_flush(void)
{
    data_manager_ctx_t *ctx = data_manager_instance();
    esp_err_t err;
    bool wrote_any = false;
    size_t index;

    if(!ctx->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    for(index = 0; index < DATA_MANAGER_STRESS_ITEM_COUNT; index++) {
        if(ctx->executed_dirty[index]) {
            wrote_any = true;
        }

        err = data_manager_flush_executed_index(index);
        if(err != ESP_OK) {
            return err;
        }
    }

    if(!wrote_any) {
        return ESP_OK;
    }

    err = nvs_commit(ctx->nvs_handle);
    if(err == ESP_OK) {
        ctx->last_executed_flush_ms = data_manager_now_ms();
    }
    return err;
}

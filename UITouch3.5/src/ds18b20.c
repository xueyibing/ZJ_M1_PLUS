#include "ds18b20.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#include "gpio.h"

static const char *TAG = "ds18b20";

#define DS18B20_CMD_SKIP_ROM          0xCCU
#define DS18B20_CMD_CONVERT_T         0x44U
#define DS18B20_CMD_READ_SCRATCHPAD   0xBEU
#define DS18B20_SCRATCHPAD_SIZE       9U
#define DS18B20_CONVERSION_TIME_MS    750U
#define DS18B20_SAMPLE_INTERVAL_MS    2000U
#define DS18B20_RETRY_INTERVAL_MS     5000U
#define DS18B20_INVALIDATE_FAILURES   3U
#define DS18B20_TEMP_MIN_RAW          (-55 * 16)
#define DS18B20_TEMP_MAX_RAW          (125 * 16)

static TaskHandle_t s_task;
static portMUX_TYPE s_bus_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_reading_lock = portMUX_INITIALIZER_UNLOCKED;
static ds18b20_reading_t s_reading = {
    .valid = false,
    .last_error = ESP_ERR_INVALID_STATE,
};

static inline void ds18b20_drive_low(void)
{
    gpio_set_level(PIN_DS18B20_DQ, 0);
}

static inline void ds18b20_release_bus(void)
{
    gpio_set_level(PIN_DS18B20_DQ, 1);
}

static bool ds18b20_reset_locked(void)
{
    bool present;

    ds18b20_drive_low();
    esp_rom_delay_us(480U);
    ds18b20_release_bus();
    esp_rom_delay_us(70U);
    present = gpio_get_level(PIN_DS18B20_DQ) == 0;
    esp_rom_delay_us(410U);
    return present;
}

static void ds18b20_write_bit_locked(bool bit)
{
    ds18b20_drive_low();
    if(bit) {
        esp_rom_delay_us(6U);
        ds18b20_release_bus();
        esp_rom_delay_us(64U);
    } else {
        esp_rom_delay_us(60U);
        ds18b20_release_bus();
        esp_rom_delay_us(10U);
    }
}

static bool ds18b20_read_bit_locked(void)
{
    bool bit;

    ds18b20_drive_low();
    esp_rom_delay_us(3U);
    ds18b20_release_bus();
    esp_rom_delay_us(10U);
    bit = gpio_get_level(PIN_DS18B20_DQ) != 0;
    esp_rom_delay_us(53U);
    return bit;
}

static void ds18b20_write_byte_locked(uint8_t value)
{
    for(uint8_t bit = 0; bit < 8U; ++bit) {
        ds18b20_write_bit_locked((value & 0x01U) != 0U);
        value >>= 1U;
    }
}

static uint8_t ds18b20_read_byte_locked(void)
{
    uint8_t value = 0;

    for(uint8_t bit = 0; bit < 8U; ++bit) {
        if(ds18b20_read_bit_locked()) {
            value |= (uint8_t)(1U << bit);
        }
    }
    return value;
}

static uint8_t ds18b20_crc8(const uint8_t *data, size_t size)
{
    uint8_t crc = 0;

    for(size_t i = 0; i < size; ++i) {
        uint8_t in = data[i];
        for(uint8_t bit = 0; bit < 8U; ++bit) {
            const uint8_t mix = (uint8_t)((crc ^ in) & 0x01U);
            crc >>= 1U;
            if(mix != 0U) {
                crc ^= 0x8CU;
            }
            in >>= 1U;
        }
    }
    return crc;
}

static esp_err_t ds18b20_start_conversion(void)
{
    bool present;

    portENTER_CRITICAL(&s_bus_lock);
    present = ds18b20_reset_locked();
    if(present) {
        ds18b20_write_byte_locked(DS18B20_CMD_SKIP_ROM);
        ds18b20_write_byte_locked(DS18B20_CMD_CONVERT_T);
    }
    portEXIT_CRITICAL(&s_bus_lock);
    return present ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t ds18b20_read_temperature(int32_t *temperature_centi_c)
{
    uint8_t scratchpad[DS18B20_SCRATCHPAD_SIZE];
    bool present;
    int16_t raw;

    if(temperature_centi_c == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_bus_lock);
    present = ds18b20_reset_locked();
    if(present) {
        ds18b20_write_byte_locked(DS18B20_CMD_SKIP_ROM);
        ds18b20_write_byte_locked(DS18B20_CMD_READ_SCRATCHPAD);
        for(size_t i = 0; i < sizeof(scratchpad); ++i) {
            scratchpad[i] = ds18b20_read_byte_locked();
        }
    }
    portEXIT_CRITICAL(&s_bus_lock);

    if(!present) {
        return ESP_ERR_NOT_FOUND;
    }
    if(ds18b20_crc8(scratchpad, DS18B20_SCRATCHPAD_SIZE - 1U) !=
       scratchpad[DS18B20_SCRATCHPAD_SIZE - 1U]) {
        return ESP_ERR_INVALID_CRC;
    }

    raw = (int16_t)(((uint16_t)scratchpad[1] << 8U) | scratchpad[0]);
    if(raw < DS18B20_TEMP_MIN_RAW || raw > DS18B20_TEMP_MAX_RAW) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *temperature_centi_c = raw >= 0
                               ? ((int32_t)raw * 100 + 8) / 16
                               : ((int32_t)raw * 100 - 8) / 16;
    return ESP_OK;
}

static esp_err_t ds18b20_measure(int32_t *temperature_centi_c)
{
    esp_err_t err = ds18b20_start_conversion();

    if(err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(DS18B20_CONVERSION_TIME_MS));
    return ds18b20_read_temperature(temperature_centi_c);
}

static void ds18b20_store_success(int32_t temperature_centi_c)
{
    taskENTER_CRITICAL(&s_reading_lock);
    s_reading.valid = true;
    s_reading.temperature_centi_c = temperature_centi_c;
    ++s_reading.sequence;
    s_reading.last_error = ESP_OK;
    taskEXIT_CRITICAL(&s_reading_lock);
}

static void ds18b20_store_error(esp_err_t error, bool invalidate)
{
    taskENTER_CRITICAL(&s_reading_lock);
    if(invalidate) {
        s_reading.valid = false;
    }
    s_reading.last_error = error;
    taskEXIT_CRITICAL(&s_reading_lock);
}

static void ds18b20_task(void *arg)
{
    bool first_sample = true;
    uint32_t successful_samples = 0;
    uint32_t consecutive_failures = 0;

    (void)arg;
    for(;;) {
        int32_t temperature_centi_c = 0;
        const esp_err_t err = ds18b20_measure(&temperature_centi_c);

        if(err == ESP_OK) {
            ds18b20_store_success(temperature_centi_c);
            consecutive_failures = 0;
            ++successful_samples;
            if(first_sample || successful_samples % 20U == 0U) {
                ESP_LOGI(TAG, "%s sample: %.2f C",
                         first_sample ? "first" : "periodic",
                         (double)temperature_centi_c / 100.0);
            }
            first_sample = false;
            vTaskDelay(pdMS_TO_TICKS(DS18B20_SAMPLE_INTERVAL_MS));
            continue;
        }

        ++consecutive_failures;
        ds18b20_store_error(err, consecutive_failures >= DS18B20_INVALIDATE_FAILURES);
        ESP_LOGW(TAG, "measurement failed (%u/%u): %s",
                 (unsigned)consecutive_failures,
                 (unsigned)DS18B20_INVALIDATE_FAILURES,
                 esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(DS18B20_RETRY_INTERVAL_MS));
    }
}

esp_err_t ds18b20_init(void)
{
    if(s_task != NULL) {
        return ESP_OK;
    }
    if(PIN_DS18B20_DQ < 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    const gpio_config_t io_config = {
        .pin_bit_mask = 1ULL << PIN_DS18B20_DQ,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_config);
    if(err != ESP_OK) {
        return err;
    }
    ds18b20_release_bus();

    if(xTaskCreate(ds18b20_task, "ds18b20", 3072, NULL, 3, &s_task) != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "sampling started, DQ=GPIO%d", PIN_DS18B20_DQ);
    return ESP_OK;
}

bool ds18b20_get_reading(ds18b20_reading_t *out)
{
    if(out == NULL) {
        return false;
    }

    taskENTER_CRITICAL(&s_reading_lock);
    memcpy(out, &s_reading, sizeof(*out));
    taskEXIT_CRITICAL(&s_reading_lock);
    return out->valid;
}

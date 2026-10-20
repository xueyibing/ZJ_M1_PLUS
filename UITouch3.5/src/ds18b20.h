#ifndef DS18B20_H
#define DS18B20_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool valid;
    int32_t temperature_centi_c;
    uint32_t sequence;
    esp_err_t last_error;
} ds18b20_reading_t;

/** Initialize the DS18B20 one-wire bus and background sampling task. */
esp_err_t ds18b20_init(void);

/** Copy the latest sample. Returns true only when a valid sample is available. */
bool ds18b20_get_reading(ds18b20_reading_t *out);

#endif /* DS18B20_H */

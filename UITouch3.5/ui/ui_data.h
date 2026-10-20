#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    UI_OIL_LIGHT = 0,
    UI_OIL_MEDIUM,
    UI_OIL_HEAVY,
    UI_OIL_COUNT,
} ui_oil_mode_t;

typedef enum {
    UI_DON_WELL = 0,
    UI_DON_JAMMY,
    UI_DON_SOFT_YOLK,
    UI_DON_SUNNY,
    UI_DON_COUNT,
} ui_doneness_t;

#define UI_MAX_MAKE_QUANTITY 6U
#define UI_SUNNY_MAX_MAKE_QUANTITY 2U

typedef enum {
    UI_ERROR_ALL_OK = 0,
    UI_ERROR_OIL_EMPTY = 1,
    UI_ERROR_EGG_EMPTY = 2,
    UI_ERROR_BEAT_NOT_EMPTY = 3,
    UI_ERROR_POTA_TMP_SENSOR_ERROR = 4,
    UI_ERROR_POTB_TMP_SENSOR_ERROR = 5,
    UI_ERROR_POTA_NO = 6,
    UI_ERROR_POTB_NO = 7,
    UI_ERROR_POTA_NO_COIL = 8,
    UI_ERROR_POTB_NO_COIL = 9,
    UI_ERROR_BEAT_SHELL_NOT_CLEAN = 10,
    UI_ERROR_POTA_OUT_EGG_FAIL = 11,
    UI_ERROR_POTA_IIC_ERROR = 12,
    UI_ERROR_POTB_IIC_ERROR = 13,
    UI_ERROR_VL53_HAVE_ERROR = 14,
    UI_ERROR_VL53_HAVE_BLOCK = 15,
    UI_ERROR_VL53_POTA_ERROR = 16,
    UI_ERROR_VL53_POTA_BLOCK = 17,
    UI_ERROR_POTB_OVERHEAT = 18,
    UI_ERROR_POTA_OVERHEAT = 19,
    UI_ERROR_UNKNOWN_ERROR = 20,
    UI_ERROR_MOTOR_DRIVER_ERROR = 21,
} ui_error_code_t;

typedef enum {
    UI_MAKER_IDLE = 0,
    UI_MAKER_CHECKING_TMP_SENSOR,
    UI_MAKER_CHECKING_VL53,
    UI_MAKER_CHECKING_POT,
    UI_MAKER_HOMEING,
    /* Protocol value 5 belonged to a removed legacy calibration state. */
    UI_MAKER_PRE_HEATING = 6,
    UI_MAKER_GIVING_EGG,
    UI_MAKER_GIVING_OIL,
    UI_MAKER_MAKING,
    UI_MAKER_STOPPING,
    UI_MAKER_STOPPED_NEEDS_RECOVERY,
} ui_maker_state_t;

typedef enum {
    UI_DATA_EVENT_OIL_CHANGED = 0,
    UI_DATA_EVENT_DONENESS_CHANGED,
    UI_DATA_EVENT_NEED_MAKE_SET,
    UI_DATA_EVENT_NEED_MAKE_INCREASED,
    UI_DATA_EVENT_NEED_MAKE_DECREASED,
    UI_DATA_EVENT_START,
    UI_DATA_EVENT_CANCEL,
    UI_DATA_EVENT_ERROR_CLEARED,
    UI_DATA_EVENT_PROGRESS,
    UI_DATA_EVENT_FINISHED,
    UI_DATA_EVENT_RESET,
} ui_data_event_t;

typedef struct {
    uint8_t needMake;
    ui_oil_mode_t oil_mode;
    ui_doneness_t doneness;
    uint16_t elapsed_s;
    uint16_t total_s;
    uint16_t remaining_s;
    bool start;
    ui_error_code_t error_code;
    ui_maker_state_t maker_state;
} ui_data_state_t;

typedef void (*ui_data_listener_t)(ui_data_event_t event,
                                   const ui_data_state_t *state,
                                   void *user_data);

typedef struct {
    ui_data_state_t state;
    ui_data_listener_t listener;
    void *listener_user_data;
    bool remote_time_authoritative;
} ui_data_t;

void ui_data_init(ui_data_t *data);
void ui_data_set_listener(ui_data_t *data, ui_data_listener_t listener, void *user_data);
const ui_data_state_t *ui_data_get_state(const ui_data_t *data);

bool ui_data_state_is_idle(const ui_data_state_t *state);
bool ui_data_state_is_running(const ui_data_state_t *state);
bool ui_data_state_is_finished(const ui_data_state_t *state);
bool ui_data_state_has_alert(const ui_data_state_t *state);
uint8_t ui_data_max_make_quantity(ui_doneness_t doneness);
bool ui_data_apply_remote_state(ui_data_t *data,
                                bool start,
                                int32_t error_code,
                                uint8_t need_make,
                                int32_t remaining_s,
                                int32_t total_make_s,
                                int32_t maker_state);
bool ui_data_set_remote_time_authoritative(ui_data_t *data, bool enabled);

bool ui_data_set_oil_mode(ui_data_t *data, ui_oil_mode_t oil_mode);
bool ui_data_set_doneness(ui_data_t *data, ui_doneness_t doneness);
bool ui_data_set_need_make(ui_data_t *data, uint8_t value);
bool ui_data_adjust_need_make(ui_data_t *data, int32_t delta);
bool ui_data_clear_need_make(ui_data_t *data);
bool ui_data_start(ui_data_t *data);
bool ui_data_cancel(ui_data_t *data);
bool ui_data_clear_error(ui_data_t *data);
bool ui_data_tick(ui_data_t *data, uint16_t delta_s);
bool ui_data_ack_finished(ui_data_t *data);

#include "ui_data.h"

#include <stddef.h>

static const uint16_t k_doneness_seconds[UI_DON_COUNT] = {
    300,
    180,
    240,
    120,
};

uint8_t ui_data_max_make_quantity(ui_doneness_t doneness)
{
    return doneness == UI_DON_SUNNY ?
           UI_SUNNY_MAX_MAKE_QUANTITY : UI_MAX_MAKE_QUANTITY;
}

static ui_error_code_t ui_data_normalize_error_code(int32_t error_code)
{
    if(error_code == (int32_t)UI_ERROR_ALL_OK) {
        return UI_ERROR_ALL_OK;
    }

    if(error_code >= (int32_t)UI_ERROR_OIL_EMPTY &&
       error_code <= (int32_t)UI_ERROR_MOTOR_DRIVER_ERROR) {
        return (ui_error_code_t)error_code;
    }

    return UI_ERROR_UNKNOWN_ERROR;
}

static ui_maker_state_t ui_data_normalize_maker_state(int32_t maker_state)
{
    if(maker_state == (int32_t)UI_MAKER_IDLE) {
        return UI_MAKER_IDLE;
    }

    if(maker_state >= (int32_t)UI_MAKER_CHECKING_TMP_SENSOR &&
       maker_state <= (int32_t)UI_MAKER_STOPPED_NEEDS_RECOVERY) {
        return (ui_maker_state_t)maker_state;
    }

    return UI_MAKER_IDLE;
}

static void ui_data_emit(ui_data_t *data, ui_data_event_t event)
{
    if(data != NULL && data->listener != NULL) {
        data->listener(event, &data->state, data->listener_user_data);
    }
}

bool ui_data_state_is_idle(const ui_data_state_t *state)
{
    return state != NULL && !state->start && state->total_s == 0U &&
           state->maker_state == UI_MAKER_IDLE;
}

bool ui_data_state_is_running(const ui_data_state_t *state)
{
    return state != NULL && state->start;
}

bool ui_data_state_is_finished(const ui_data_state_t *state)
{
    return state != NULL && !state->start && state->total_s > 0U;
}

bool ui_data_state_has_alert(const ui_data_state_t *state)
{
    return state != NULL && state->error_code != UI_ERROR_ALL_OK;
}

bool ui_data_apply_remote_state(ui_data_t *data,
                                bool start,
                                int32_t error_code,
                                uint8_t need_make,
                                int32_t remaining_s,
                                int32_t total_make_s,
                                int32_t maker_state)
{
    bool changed = false;
    uint8_t clamped_need_make;
    uint16_t clamped_remaining_s = 0U;
    uint16_t clamped_total_make_s = 0U;
    ui_error_code_t normalized_error_code;
    ui_maker_state_t normalized_maker_state;
    bool completed_normally;

    if(data == NULL) {
        return false;
    }

    normalized_error_code = ui_data_normalize_error_code(error_code);
    normalized_maker_state = ui_data_normalize_maker_state(maker_state);
    clamped_need_make = need_make > 9U ? 9U : need_make;
    completed_normally = data->state.start && !start &&
                         normalized_error_code == UI_ERROR_ALL_OK &&
                         clamped_need_make == 0U &&
                         normalized_maker_state == UI_MAKER_IDLE;
    if(remaining_s >= 0) {
        clamped_remaining_s = remaining_s > 65535 ? 65535U : (uint16_t)remaining_s;
    }
    if(total_make_s >= 0) {
        clamped_total_make_s = total_make_s > 65535 ? 65535U : (uint16_t)total_make_s;
    }

    if(data->state.error_code != normalized_error_code) {
        data->state.error_code = normalized_error_code;
        changed = true;
    }

    if(maker_state >= 0) {
        if(data->state.maker_state != normalized_maker_state) {
            data->state.maker_state = normalized_maker_state;
            changed = true;
        }
    } else if(!start && data->state.maker_state != UI_MAKER_IDLE) {
        data->state.maker_state = UI_MAKER_IDLE;
        changed = true;
    }

    if(data->state.needMake != clamped_need_make) {
        data->state.needMake = clamped_need_make;
        changed = true;
    }

    if(data->state.start != start) {
        data->state.start = start;
        changed = true;
    }

    if(start && remaining_s >= 0 && total_make_s >= 0) {
        uint16_t next_total_s = 0U;
        uint16_t next_remaining_s = 0U;
        uint16_t next_elapsed_s = 0U;

        /* 主控的计时字段描述当前单面制作。检测、回零、预热等阶段
         * total_make_s 为 0，此时保持进度为空，不制造伪倒计时。 */
        if(normalized_maker_state == UI_MAKER_MAKING && clamped_total_make_s > 0U) {
            next_total_s = clamped_total_make_s;
            next_remaining_s = clamped_remaining_s > next_total_s ?
                               next_total_s : clamped_remaining_s;
            next_elapsed_s = (uint16_t)(next_total_s - next_remaining_s);
        }

        if(data->state.total_s != next_total_s ||
           data->state.remaining_s != next_remaining_s ||
           data->state.elapsed_s != next_elapsed_s) {
            data->state.total_s = next_total_s;
            data->state.remaining_s = next_remaining_s;
            data->state.elapsed_s = next_elapsed_s;
            changed = true;
        }
        if(!data->remote_time_authoritative) {
            data->remote_time_authoritative = true;
            changed = true;
        }
    }

    if(!start && completed_normally && data->state.total_s > 0U) {
        if(data->state.elapsed_s != data->state.total_s || data->state.remaining_s != 0U) {
            data->state.elapsed_s = data->state.total_s;
            data->state.remaining_s = 0U;
            changed = true;
        }
    } else if(!start &&
              (data->state.elapsed_s != 0U || data->state.total_s != 0U || data->state.remaining_s != 0U)) {
        data->state.elapsed_s = 0U;
        data->state.total_s = 0U;
        data->state.remaining_s = 0U;
        changed = true;
    }

    if(!start && data->remote_time_authoritative) {
        data->remote_time_authoritative = false;
        changed = true;
    }

    return changed;
}

bool ui_data_set_remote_time_authoritative(ui_data_t *data, bool enabled)
{
    if(data == NULL || data->remote_time_authoritative == enabled) {
        return false;
    }

    data->remote_time_authoritative = enabled;
    return true;
}

static bool ui_data_is_idle(const ui_data_t *data)
{
    return data != NULL && ui_data_state_is_idle(&data->state);
}

void ui_data_init(ui_data_t *data)
{
    if(data == NULL) {
        return;
    }

    data->state.needMake = 0;
    data->state.oil_mode = UI_OIL_MEDIUM;
    data->state.doneness = UI_DON_JAMMY;
    data->state.elapsed_s = 0;
    data->state.total_s = 0;
    data->state.remaining_s = 0;
    data->state.start = false;
    data->state.error_code = UI_ERROR_ALL_OK;
    data->state.maker_state = UI_MAKER_IDLE;
    data->listener = NULL;
    data->listener_user_data = NULL;
    data->remote_time_authoritative = false;
}

void ui_data_set_listener(ui_data_t *data, ui_data_listener_t listener, void *user_data)
{
    if(data == NULL) {
        return;
    }

    data->listener = listener;
    data->listener_user_data = user_data;
}

const ui_data_state_t *ui_data_get_state(const ui_data_t *data)
{
    if(data == NULL) {
        return NULL;
    }

    return &data->state;
}

bool ui_data_set_oil_mode(ui_data_t *data, ui_oil_mode_t oil_mode)
{
    bool idle;
    bool running;

    if(data == NULL || oil_mode >= UI_OIL_COUNT || data->state.oil_mode == oil_mode) {
        return false;
    }

    idle = ui_data_is_idle(data);
    running = ui_data_state_is_running(&data->state);
    if(!idle && !running) {
        return false;
    }

    data->state.oil_mode = oil_mode;
    ui_data_emit(data, UI_DATA_EVENT_OIL_CHANGED);
    return true;
}

bool ui_data_set_doneness(ui_data_t *data, ui_doneness_t doneness)
{
    bool idle;
    bool running;

    if(data == NULL || doneness >= UI_DON_COUNT || data->state.doneness == doneness) {
        return false;
    }

    idle = ui_data_is_idle(data);
    running = ui_data_state_is_running(&data->state);
    if(!idle && !running) {
        return false;
    }

    data->state.doneness = doneness;
    ui_data_emit(data, UI_DATA_EVENT_DONENESS_CHANGED);

    /* Sunny-side-up uses both pans at once, so one run can make at most two. */
    if(idle && data->state.needMake > ui_data_max_make_quantity(doneness)) {
        data->state.needMake = ui_data_max_make_quantity(doneness);
        ui_data_emit(data, UI_DATA_EVENT_NEED_MAKE_SET);
    }
    return true;
}

bool ui_data_set_need_make(ui_data_t *data, uint8_t value)
{
    if(data == NULL || !ui_data_is_idle(data) ||
       value > ui_data_max_make_quantity(data->state.doneness) ||
       data->state.needMake == value) {
        return false;
    }

    data->state.needMake = value;
    ui_data_emit(data, UI_DATA_EVENT_NEED_MAKE_SET);
    return true;
}

bool ui_data_adjust_need_make(ui_data_t *data, int32_t delta)
{
    int32_t next_value;
    int32_t max_value;
    uint8_t previous_need_make;
    bool idle;
    bool running;

    if(data == NULL || delta == 0) {
        return false;
    }

    idle = ui_data_is_idle(data);
    running = ui_data_state_is_running(&data->state);
    if(!idle && !running) {
        return false;
    }

    if(running && delta < 0 && data->state.needMake <= 1U) {
        return false;
    }

    previous_need_make = data->state.needMake;
    next_value = (int32_t)previous_need_make + delta;
    max_value = (int32_t)ui_data_max_make_quantity(data->state.doneness);
    if(next_value < 0) {
        next_value = 0;
    }
    if(next_value > max_value) {
        next_value = max_value;
    }

    if(next_value == (int32_t)data->state.needMake) {
        return false;
    }

    data->state.needMake = (uint8_t)next_value;
    ui_data_emit(data,
                 next_value > (int32_t)previous_need_make ?
                 UI_DATA_EVENT_NEED_MAKE_INCREASED :
                 UI_DATA_EVENT_NEED_MAKE_DECREASED);
    return true;
}

bool ui_data_clear_need_make(ui_data_t *data)
{
    if(data == NULL || !ui_data_is_idle(data) || data->state.needMake == 0U) {
        return false;
    }

    data->state.needMake = 0U;
    ui_data_emit(data, UI_DATA_EVENT_RESET);
    return true;
}

bool ui_data_start(ui_data_t *data)
{
    if(!ui_data_is_idle(data) || data->state.needMake == 0U ||
       data->state.needMake > ui_data_max_make_quantity(data->state.doneness)) {
        return false;
    }

    data->state.elapsed_s = 0;
    data->state.total_s = (uint16_t)(k_doneness_seconds[data->state.doneness] +
                                     ((data->state.needMake - 1U) * 15U));
    data->state.remaining_s = data->state.total_s;
    data->state.start = true;
    data->state.maker_state = UI_MAKER_IDLE;
    data->remote_time_authoritative = false;
    ui_data_emit(data, UI_DATA_EVENT_START);
    return true;
}

bool ui_data_cancel(ui_data_t *data)
{
    if(data == NULL) {
        return false;
    }

    if(ui_data_state_is_idle(&data->state) && data->state.needMake == 0U) {
        return false;
    }

    data->state.needMake = 0;
    data->state.elapsed_s = 0;
    data->state.total_s = 0;
    data->state.remaining_s = 0;
    data->state.start = false;
    data->state.maker_state = UI_MAKER_IDLE;
    data->remote_time_authoritative = false;
    ui_data_emit(data, UI_DATA_EVENT_CANCEL);
    return true;
}

bool ui_data_clear_error(ui_data_t *data)
{
    if(data == NULL || data->state.error_code == UI_ERROR_ALL_OK) {
        return false;
    }

    data->state.error_code = UI_ERROR_ALL_OK;
    data->state.start = false;
    data->state.needMake = 0;
    data->state.elapsed_s = 0;
    data->state.total_s = 0;
    data->state.remaining_s = 0;
    data->state.maker_state = UI_MAKER_IDLE;
    data->remote_time_authoritative = false;
    ui_data_emit(data, UI_DATA_EVENT_ERROR_CLEARED);
    return true;
}

bool ui_data_tick(ui_data_t *data, uint16_t delta_s)
{
    if(data == NULL || !data->state.start || delta_s == 0U || data->remote_time_authoritative) {
        return false;
    }

    if((uint32_t)data->state.elapsed_s + delta_s >= data->state.total_s) {
        data->state.elapsed_s = data->state.total_s;
        data->state.remaining_s = 0U;
        data->state.start = false;
        data->state.maker_state = UI_MAKER_IDLE;
        ui_data_emit(data, UI_DATA_EVENT_FINISHED);
        return true;
    }

    data->state.elapsed_s = (uint16_t)(data->state.elapsed_s + delta_s);
    data->state.remaining_s = (uint16_t)(data->state.total_s - data->state.elapsed_s);
    ui_data_emit(data, UI_DATA_EVENT_PROGRESS);
    return true;
}

bool ui_data_ack_finished(ui_data_t *data)
{
    if(data == NULL || !ui_data_state_is_finished(&data->state)) {
        return false;
    }

    data->state.elapsed_s = 0;
    data->state.total_s = 0;
    data->state.remaining_s = 0;
    data->state.start = false;
    data->state.maker_state = UI_MAKER_IDLE;
    data->remote_time_authoritative = false;
    ui_data_emit(data, UI_DATA_EVENT_RESET);
    return true;
}

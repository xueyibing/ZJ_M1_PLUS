#include "ui_notice.h"

#include "buzzer.h"
#include "ui.h"
#include "ui_alert.h"
#include "ui_common.h"
#include "ui_i18n.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define UI_NOTICE_HEADLINE_MAX 48
#define UI_NOTICE_MESSAGE_MAX 192
#define UI_NOTICE_BUTTON_TEXT_MAX 16

typedef struct
{
    bool active;
    lv_obj_t *screen;
    lv_obj_t *overlay;
    lv_obj_t *card;
    lv_obj_t *message_panel;
    lv_obj_t *headline_label;
    lv_obj_t *message_label;
    ui_data_t *data;
    ui_notice_action_cb_t action_cb;
    void *action_user_data;
    ui_notice_action_cb_t secondary_action_cb;
    void *secondary_action_user_data;
    char headline[UI_NOTICE_HEADLINE_MAX];
    char message[UI_NOTICE_MESSAGE_MAX];
    char button_text[UI_NOTICE_BUTTON_TEXT_MAX];
    char secondary_button_text[UI_NOTICE_BUTTON_TEXT_MAX];
} ui_notice_view_t;

static ui_notice_view_t s_notice;

static void ui_notice_apply_content(void)
{
    if(s_notice.headline_label == NULL ||
       s_notice.message_label == NULL) {
        return;
    }

    lv_label_set_text(s_notice.headline_label,
                      s_notice.headline[0] != '\0' ? s_notice.headline : ui_t(UI_STR_NOTICE));
    lv_label_set_text(s_notice.message_label,
                      s_notice.message[0] != '\0' ? s_notice.message : ui_t(UI_STR_NOTICE_DEFAULT));
}

static void ui_notice_back_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);

    if(s_notice.action_cb != NULL &&
       !s_notice.action_cb(s_notice.action_user_data)) {
        return;
    }

    s_notice.active = false;
    ui_create(s_notice.data);
}

static void ui_notice_secondary_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);

    if(s_notice.secondary_action_cb != NULL &&
       !s_notice.secondary_action_cb(s_notice.secondary_action_user_data)) {
        return;
    }

    s_notice.active = false;
    ui_create(s_notice.data);
}

bool ui_notice_is_active(void)
{
    return s_notice.active;
}

void ui_notice_create(ui_data_t *data,
                      const char *headline,
                      const char *message,
                      const char *button_text)
{
    ui_notice_create_with_action(data, headline, message, button_text, NULL, NULL);
}

void ui_notice_create_with_action(ui_data_t *data,
                                  const char *headline,
                                  const char *message,
                                  const char *button_text,
                                  ui_notice_action_cb_t action_cb,
                                  void *action_user_data)
{
    ui_notice_create_with_two_actions(data,
                                      headline,
                                      message,
                                      button_text,
                                      action_cb,
                                      action_user_data,
                                      NULL,
                                      NULL,
                                      NULL);
}

void ui_notice_create_with_two_actions(ui_data_t *data,
                                       const char *headline,
                                       const char *message,
                                       const char *primary_button_text,
                                       ui_notice_action_cb_t primary_action_cb,
                                       void *primary_action_user_data,
                                       const char *secondary_button_text,
                                       ui_notice_action_cb_t secondary_action_cb,
                                       void *secondary_action_user_data)
{
    const ui_data_state_t *state;
    lv_obj_t *screen;
    lv_obj_t *button;
    lv_obj_t *label;

    if(data == NULL) {
        return;
    }

    state = ui_data_get_state(data);
    if(ui_alert_should_show(state)) {
        ui_alert_create(data);
        return;
    }

    if(s_notice.active && s_notice.overlay != NULL) {
        lv_obj_delete(s_notice.overlay);
    }

    lv_memzero(&s_notice, sizeof(s_notice));
    s_notice.active = true;
    s_notice.data = data;
    s_notice.action_cb = primary_action_cb;
    s_notice.action_user_data = primary_action_user_data;
    s_notice.secondary_action_cb = secondary_action_cb;
    s_notice.secondary_action_user_data = secondary_action_user_data;
    strlcpy(s_notice.headline,
            (headline != NULL && headline[0] != '\0') ? headline : ui_t(UI_STR_NOTICE),
            sizeof(s_notice.headline));
    strlcpy(s_notice.message,
            (message != NULL && message[0] != '\0') ? message : ui_t(UI_STR_NOTICE_DEFAULT),
            sizeof(s_notice.message));
    strlcpy(s_notice.button_text,
            (primary_button_text != NULL && primary_button_text[0] != '\0') ? primary_button_text : ui_t(UI_STR_CONFIRM),
            sizeof(s_notice.button_text));
    strlcpy(s_notice.secondary_button_text,
            (secondary_button_text != NULL) ? secondary_button_text : "",
            sizeof(s_notice.secondary_button_text));

    screen = lv_screen_active();
    s_notice.screen = screen;
    s_notice.overlay = lv_obj_create(screen);
    ui_dialog_style_overlay(s_notice.overlay);
    lv_obj_move_foreground(s_notice.overlay);

    s_notice.card = lv_obj_create(s_notice.overlay);
    ui_dialog_style_card(s_notice.card);

    s_notice.headline_label = lv_label_create(s_notice.card);
    ui_dialog_style_title(s_notice.headline_label);

    s_notice.message_panel = lv_obj_create(s_notice.card);
    ui_dialog_style_message_panel(s_notice.message_panel);

    s_notice.message_label = lv_label_create(s_notice.message_panel);
    ui_dialog_style_message(s_notice.message_label);

    button = lv_button_create(s_notice.card);
    ui_dialog_style_button(button,
                           s_notice.secondary_button_text[0] != '\0'
                               ? UI_DIALOG_DOUBLE_BUTTON_W
                               : UI_DIALOG_BUTTON_W,
                           true);
    lv_obj_align(button,
                 LV_ALIGN_BOTTOM_MID,
                 s_notice.secondary_button_text[0] != '\0' ? 46 : 0,
                 -UI_DIALOG_BUTTON_BOTTOM);
    lv_obj_add_event_cb(button, ui_notice_back_event_cb, LV_EVENT_CLICKED, NULL);

    label = lv_label_create(button);
    lv_label_set_text(label, s_notice.button_text);
    ui_dialog_style_button_label(label, true);

    if(s_notice.secondary_button_text[0] != '\0') {
        button = lv_button_create(s_notice.card);
        ui_dialog_style_button(button, UI_DIALOG_DOUBLE_BUTTON_W, false);
        lv_obj_align(button, LV_ALIGN_BOTTOM_MID, -46, -UI_DIALOG_BUTTON_BOTTOM);
        lv_obj_add_event_cb(button, ui_notice_secondary_event_cb, LV_EVENT_CLICKED, NULL);

        label = lv_label_create(button);
        lv_label_set_text(label, s_notice.secondary_button_text);
        ui_dialog_style_button_label(label, false);
    }

    ui_notice_apply_content();
}

void ui_notice_on_data_changed(void)
{
    const ui_data_state_t *state;

    if(!s_notice.active || s_notice.data == NULL) {
        return;
    }

    state = ui_data_get_state(s_notice.data);
    if(ui_alert_should_show(state)) {
        s_notice.active = false;
        ui_alert_create(s_notice.data);
        return;
    }

    ui_notice_apply_content();
}

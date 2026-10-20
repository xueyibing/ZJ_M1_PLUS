#pragma once

#include <stdbool.h>

#include "ui_data.h"

// 返回true关闭提示页；返回false表示条件未满足，提示页保持打开。
typedef bool (*ui_notice_action_cb_t)(void *user_data);

void ui_notice_create(ui_data_t *data,
                      const char *headline,
                      const char *message,
                      const char *button_text);
void ui_notice_create_with_action(ui_data_t *data,
                                  const char *headline,
                                  const char *message,
                                  const char *button_text,
                                  ui_notice_action_cb_t action_cb,
                                  void *action_user_data);
void ui_notice_create_with_two_actions(ui_data_t *data,
                                       const char *headline,
                                       const char *message,
                                       const char *primary_button_text,
                                       ui_notice_action_cb_t primary_action_cb,
                                       void *primary_action_user_data,
                                       const char *secondary_button_text,
                                       ui_notice_action_cb_t secondary_action_cb,
                                       void *secondary_action_user_data);
void ui_notice_on_data_changed(void);
bool ui_notice_is_active(void);

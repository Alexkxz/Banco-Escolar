#pragma once

#include "terminal_capture_ui_state.h"

bool terminal_capture_init();
bool terminal_capture_request(bool show_ui = false);
bool terminal_capture_get_ui_snapshot(terminal_capture_ui::Snapshot *snapshot);
void terminal_capture_ui_view_ready();
void terminal_capture_ui_dismiss();
void terminal_capture_poll();

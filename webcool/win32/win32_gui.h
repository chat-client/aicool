#pragma once

#ifdef _WIN32
#include "webcool_controller.h"

void ensure_console_for_cli();
int run_windows_control_gui(webcool_controller& controller);

#endif // _WIN32


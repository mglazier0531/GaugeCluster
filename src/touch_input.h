#pragma once
#include "driver/i2c_master.h"
#include "display_settings.h"
bool touch_input_init(i2c_master_bus_handle_t bus);
bool touch_input_next(TouchSample& sample);

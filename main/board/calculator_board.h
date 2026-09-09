#pragma once
#include "driver/i2c_master.h"
#include "esp_err.h"

esp_err_t calculator_board_init();
i2c_master_bus_handle_t calculator_board_sensor_i2c_bus();

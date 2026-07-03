#pragma once
// Fake ESP-IDF driver/i2c.h — just the I2C_NUM_0 constant board_config.h
// needs. The real I2C bus is never touched: FT3168.cpp/qmi8658c.cpp (which
// call the real I2C functions) aren't compiled in the emulator.
#include <stdint.h>
typedef enum { I2C_NUM_0 = 0, I2C_NUM_1 = 1 } i2c_port_t;

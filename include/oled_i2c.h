#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool oled_i2c_init(void);
int oled_i2c_write(uint8_t address, const uint8_t *data, size_t size, unsigned timeout_us);

#pragma once
#include <stdint.h>
void ws2812_init(void);
void ws2812_set(unsigned i, uint8_t r, uint8_t g, uint8_t b);
void ws2812_show(void);
void ws2812_clear(void);
void ws2812_task(void);

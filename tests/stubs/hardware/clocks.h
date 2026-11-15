#pragma once
#include <stdint.h>
enum { clk_sys };
static inline uint32_t clock_get_hz(unsigned clock) { (void)clock; return 250000000; }

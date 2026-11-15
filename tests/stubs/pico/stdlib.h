#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Host-only time for speech tests. pvs.c itself needs no timing functions. */
typedef uint64_t absolute_time_t;
extern uint64_t test_time_us;
#define PICO_BOARD "host-test"
static inline absolute_time_t make_timeout_time_ms(unsigned ms) { return test_time_us + (uint64_t)ms * 1000u; }
static inline bool time_reached(absolute_time_t at) { return test_time_us >= at; }
static inline void tight_loop_contents(void) { test_time_us += 1000u; }

static inline uint32_t time_us_32(void) { return (uint32_t)test_time_us; }

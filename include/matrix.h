#pragma once
#include <stdint.h>
typedef struct {
    uint16_t down, pressed, released;
    uint32_t time_ms;
} matrix_state_t;
void matrix_init(void);
/* Capture autonomous scan history without consuming button events. */
void matrix_task(void);
matrix_state_t matrix_scan(void);
void matrix_print_status(void);

#pragma once
#include <stddef.h>
#include <stdint.h>
void pvs_memory_init(void);
void pvs_memory_print_status(void);
/* Reserved at boot, never resized or freed; used by the raw PCM cache. */
uint8_t *pvs_sample_buffer(size_t *capacity);

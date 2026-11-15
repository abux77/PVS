#pragma once
#include <stdbool.h>
void pvs_init(void);
void pvs_task(void);
void pvs_print_status(void);
/* Reload user-edited startup state, banks, locks and presets after USB eject. */
void pvs_reload_storage(void);

void pvs_request_bootsel(void);
bool pvs_bootsel_ready(void);

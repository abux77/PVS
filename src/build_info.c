#include <stdio.h>
#include "build_info.h"
#include "pvs_build_generated.h"
void pvs_build_print_status(void) {
    printf("Firmware build: %u (%s)\n", PVS_BUILD_NUMBER, PVS_BUILD_TIME);
}

unsigned pvs_build_number(void) { return PVS_BUILD_NUMBER; }

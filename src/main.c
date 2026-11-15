#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/clocks.h"
#include "tusb.h"
#include "pvs.h"

int main(void) {
    /* Match main.py: the two chips use different known reference clocks. */
#if PICO_RP2350
    const uint32_t clock_khz = 150000;
#else
    const uint32_t clock_khz = 250000;
#endif
    if (!set_sys_clock_khz(clock_khz, false)) {
        panic("Unable to configure PVS reference clock");
    }

    /* Composite CDC + MSC owns TinyUSB. SDK stdio is a CDC client; the main
     * loop and audio service callback explicitly pump tud_task(). */
    if (!tusb_init()) {
        panic("Unable to initialize PVS USB");
    }
    stdio_init_all();
    printf("PVS native C, board=%s, CPU=%lu MHz\n", PICO_BOARD,
           (unsigned long)(clock_get_hz(clk_sys) / 1000000u));
    pvs_init();
    while (true) {
        pvs_task();
        if (pvs_bootsel_ready()) reset_usb_boot(0, 0);
        tight_loop_contents();
    }
}

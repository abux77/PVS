#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "ws2812.h"
#include "pvs_config.h"
#include "ws2812.pio.h"

/* PIO1 SM1: joined TX FIFO holds all eight pixels. No DMA, IRQ or interrupt
 * masking is needed. Frames coalesce while the wire is busy. */
#define LED_SM 1u
static uint32_t pixels[PVS_LED_COUNT];
static bool pending;
static uint32_t last_frame;

void ws2812_init(void) {
    pio_sm_claim(pio1, LED_SM);
    unsigned offset = pio_add_program(pio1, &pvs_ws2812_program);
    pio_gpio_init(pio1, PVS_WS2812_PIN);
    pio_sm_set_consecutive_pindirs(pio1, LED_SM, PVS_WS2812_PIN, 1, true);
    pio_sm_config config = pvs_ws2812_program_get_default_config(offset);
    sm_config_set_sideset_pins(&config, PVS_WS2812_PIN);
    sm_config_set_out_shift(&config, false, true, 24);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&config, (float)clock_get_hz(clk_sys) / 8000000.0f);
    pio_sm_init(pio1, LED_SM, offset, &config);
    pio_sm_set_enabled(pio1, LED_SM, true);
    last_frame = time_us_32() - 400u;
    ws2812_clear();
    ws2812_show();
}

void ws2812_set(unsigned i, uint8_t r, uint8_t g, uint8_t b) {
    if (i < PVS_LED_COUNT)
        pixels[i] = ((uint32_t)g << 24u) | ((uint32_t)r << 16u) | ((uint32_t)b << 8u);
}

void ws2812_task(void) {
    uint32_t now = time_us_32();
    if (!pending || (uint32_t)(now - last_frame) < 400u ||
        !pio_sm_is_tx_fifo_empty(pio1, LED_SM)) return;
    for (unsigned i = 0; i < PVS_LED_COUNT; ++i)
        pio_sm_put(pio1, LED_SM, pixels[i]);
    last_frame = now;
    pending = false;
}

void ws2812_show(void) {
    pending = true;
    ws2812_task();
}

void ws2812_clear(void) {
    for (unsigned i = 0; i < PVS_LED_COUNT; ++i) pixels[i] = 0;
}

/* OLED-only write transport: PIO0 SM1, 7 instructions, no DMA or IRQ.
 * PIO0 SM0 remains available for comparison audio; matrix/LEDs use PIO1.
 * Interrupts stay enabled throughout. Timeout covers the complete transfer.
 */
#include "oled_i2c.h"
#include "pvs_config.h"
#include "oled_i2c.pio.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"

#define OLED_SM 1u
#define BUS_MASK ((1u << PVS_OLED_SDA) | (1u << PVS_OLED_SCL))
static unsigned offset;
static bool ready;

static bool expired(uint32_t start, unsigned timeout) {
    return (uint32_t)(time_us_32() - start) >= timeout;
}
/* Only called with SM disabled. Never drive either wire high. */
static void lines(bool sda_low, bool scl_low) {
    pio_sm_set_pindirs_with_mask(pio0, OLED_SM,
        (sda_low ? 1u << PVS_OLED_SDA : 0) |
        (scl_low ? 1u << PVS_OLED_SCL : 0), BUS_MASK);
    busy_wait_us_32(2);
}
static bool clock_high(uint32_t start, unsigned timeout) {
    while (!gpio_get(PVS_OLED_SCL)) {
        if (expired(start, timeout)) return false;
        tight_loop_contents();
    }
    return true;
}
static void reset_sm(void) {
    pio_sm_set_enabled(pio0, OLED_SM, false);
    pio_sm_clear_fifos(pio0, OLED_SM);
    pio_sm_restart(pio0, OLED_SM);
    pio_sm_exec(pio0, OLED_SM, pio_encode_jmp(offset) | pio_encode_sideset(1, 1));
}
bool oled_i2c_init(void) {
    if (ready) return true;
    if (pio_sm_is_claimed(pio0, OLED_SM) ||
        !pio_can_add_program(pio0, &pvs_oled_i2c_program)) return false;
    pio_sm_claim(pio0, OLED_SM);
    offset = pio_add_program(pio0, &pvs_oled_i2c_program);
    pio_sm_config c = pvs_oled_i2c_program_get_default_config(offset);
    sm_config_set_out_pins(&c, PVS_OLED_SDA, 1);
    sm_config_set_in_pins(&c, PVS_OLED_SDA);
    sm_config_set_sideset_pins(&c, PVS_OLED_SCL);
    sm_config_set_jmp_pin(&c, PVS_OLED_SCL);
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_in_shift(&c, false, false, 32);
    /* At least 1 us low + 1 us high; FIFO handshakes add margin. */
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / 4000000.0f);
    pio_sm_init(pio0, OLED_SM, offset, &c);
    pio_sm_set_pins_with_mask(pio0, OLED_SM, 0, BUS_MASK);
    lines(false, false);
    gpio_pull_up(PVS_OLED_SDA);
    gpio_pull_up(PVS_OLED_SCL);
    pio_gpio_init(pio0, PVS_OLED_SDA);
    pio_gpio_init(pio0, PVS_OLED_SCL);
    ready = true;
    return true;
}
static bool bit(bool low, uint32_t start, unsigned timeout, bool *high) {
    if (expired(start, timeout)) return false;
    pio_sm_put(pio0, OLED_SM, low ? 1u : 0u);
    while (pio_sm_is_rx_fifo_empty(pio0, OLED_SM)) {
        if (expired(start, timeout)) return false;
        tight_loop_contents();
    }
    *high = (pio_sm_get(pio0, OLED_SM) & 1u) != 0;
    return true;
}
static bool byte(uint8_t value, uint32_t start, unsigned timeout) {
    bool high;
    for (unsigned i = 0; i < 8; ++i) {
        if (!bit((value & 0x80u) == 0, start, timeout, &high)) return false;
        value <<= 1;
    }
    return bit(false, start, timeout, &high) && !high;
}
int oled_i2c_write(uint8_t address, const uint8_t *data, size_t size, unsigned timeout) {
    if (!ready || address > 0x7f || (!data && size)) return -1;
    uint32_t start = time_us_32();
    reset_sm();
    lines(false, false);
    bool ok = clock_high(start, timeout) && gpio_get(PVS_OLED_SDA);
    if (ok) {
        lines(true, false); /* START: SDA falls while SCL is high. */
        lines(true, true);
        pio_sm_set_enabled(pio0, OLED_SM, true);
        ok = byte((uint8_t)(address << 1), start, timeout);
        for (size_t i = 0; ok && i < size; ++i) ok = byte(data[i], start, timeout);
    }
    reset_sm();
    lines(true, true);
    lines(true, false);
    bool stopped = clock_high(start, timeout);
    lines(false, false); /* STOP, or release both wires after a stuck clock. */
    return ok && stopped ? (int)size : -1;
}

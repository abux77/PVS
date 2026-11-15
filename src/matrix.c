#include "keypad.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "matrix.h"
#include "pvs_config.h"
#include "matrix.pio.h"

/* PIO1 SM0 owns matrix scanning; one SDK-claimed DMA channel uses its RX
 * DREQ. No IRQ is needed. The normal transfer count works on both MCUs. */
#define MATRIX_SM 0u
#define MATRIX_DMA_COUNT 0x0ffffe00u
static uint32_t history[PVS_MATRIX_RING_ENTRIES]
    __attribute__((aligned(PVS_MATRIX_RING_ENTRIES * sizeof(uint32_t))));
static matrix_state_t events[PVS_MATRIX_EVENT_COUNT];
static unsigned event_read, event_write;
static uint16_t stable, candidate;
static unsigned candidate_count;
static uint32_t consumed, overruns, event_overruns;
static int dma_channel;

static uint16_t decode_scan(uint32_t raw) {
    uint16_t result = 0;
    raw = ~raw;
    /* Reference SHIFT_LEFT nibble reversal, then transpose to physical
     * wiring, then map to printed-key order. GP6/GP10 is 1; GP9/GP13 is D. */
    for (unsigned col = 0; col < 4; ++col)
        for (unsigned row = 0; row < 4; ++row)
            if (raw & (1u << ((3u - col) * 4u + row)))
                result |= (uint16_t)(1u << keypad_logical_index(row * 4u + col));
    return result;
}

static void accept_scan(uint16_t mask, uint32_t time_ms) {
    if (mask == stable) {
        candidate = mask;
        candidate_count = 0;
        return;
    }
    if (mask != candidate) {
        candidate = mask;
        candidate_count = 1;
        return;
    }
    if (++candidate_count < PVS_MATRIX_DEBOUNCE_SCANS) return;
    matrix_state_t event = {mask, (uint16_t)(mask & ~stable),
                            (uint16_t)(stable & ~mask), time_ms};
    stable = mask;
    candidate_count = 0;
    unsigned next = (event_write + 1u) % PVS_MATRIX_EVENT_COUNT;
    if (next == event_read) {
        /* Drop the oldest complete transition, preserving newest physical
         * state. The diagnostic counter makes prolonged service gaps visible. */
        event_read = (event_read + 1u) % PVS_MATRIX_EVENT_COUNT;
        ++event_overruns;
    }
    events[event_write] = event;
    event_write = next;
}

void matrix_init(void) {
    pio_sm_claim(pio1, MATRIX_SM);
    unsigned offset = pio_add_program(pio1, &pvs_matrix_program);
    for (unsigned i = 0; i < 4; ++i) {
        pio_gpio_init(pio1, PVS_MATRIX_COL0 + i);
        gpio_disable_pulls(PVS_MATRIX_COL0 + i);
        pio_gpio_init(pio1, PVS_MATRIX_ROW0 + i);
#if PICO_RP2350
        /* Matches reference hardware: Pico 2 requires external 10k pull-ups. */
        gpio_disable_pulls(PVS_MATRIX_ROW0 + i);
#else
        gpio_pull_up(PVS_MATRIX_ROW0 + i);
#endif
    }
    pio_sm_config config = pvs_matrix_program_get_default_config(offset);
    sm_config_set_set_pins(&config, PVS_MATRIX_COL0, 4);
    sm_config_set_in_pins(&config, PVS_MATRIX_ROW0);
    sm_config_set_in_shift(&config, false, false, 32);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv(&config, (float)clock_get_hz(clk_sys) / 100000.0f);
    pio_sm_init(pio1, MATRIX_SM, offset, &config);
    pio_sm_set_consecutive_pindirs(pio1, MATRIX_SM, PVS_MATRIX_COL0, 8, false);
    pio_sm_set_pins_with_mask(pio1, MATRIX_SM, 0, 15u << PVS_MATRIX_COL0);
    dma_channel = dma_claim_unused_channel(true);
    dma_channel_config dma = dma_channel_get_default_config(dma_channel);
    channel_config_set_transfer_data_size(&dma, DMA_SIZE_32);
    channel_config_set_read_increment(&dma, false);
    channel_config_set_write_increment(&dma, true);
    channel_config_set_ring(&dma, true, 11); /* 512 32-bit samples. */
    channel_config_set_dreq(&dma, pio_get_dreq(pio1, MATRIX_SM, false));
    dma_channel_configure(dma_channel, &dma, history, &pio1->rxf[MATRIX_SM],
                          MATRIX_DMA_COUNT, true);
    pio_sm_set_enabled(pio1, MATRIX_SM, true);
}

void matrix_task(void) {
    uint32_t completed = MATRIX_DMA_COUNT - dma_channel_hw_addr(dma_channel)->transfer_count;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (completed - consumed > PVS_MATRIX_RING_ENTRIES) {
        consumed = completed - PVS_MATRIX_RING_ENTRIES;
        candidate_count = 0;
        ++overruns;
    }
    /* DMA publishes transfer_count after its memory write. */
    __compiler_memory_barrier();
    while (consumed < completed) {
        uint32_t scan = history[consumed & (PVS_MATRIX_RING_ENTRIES - 1u)];
        uint32_t age = (completed - consumed - 1u) * 2u;
        accept_scan(decode_scan(scan), now - age);
        ++consumed;
    }
    if (completed == MATRIX_DMA_COUNT) {
        consumed = 0;
        dma_channel_set_write_addr(dma_channel, history, false);
        dma_channel_set_trans_count(dma_channel, MATRIX_DMA_COUNT, true);
    }
}

matrix_state_t matrix_scan(void) {
    matrix_task();
    if (event_read != event_write) {
        matrix_state_t event = events[event_read];
        event_read = (event_read + 1u) % PVS_MATRIX_EVENT_COUNT;
        return event;
    }
    return (matrix_state_t){stable, 0, 0, to_ms_since_boot(get_absolute_time())};
}

void matrix_print_status(void) {
    printf("Matrix GP6-13 active-low: PIO1 SM0 DMA%d DREQ%u, 500Hz, debounce16ms, history overruns %lu, event overruns %lu\n",
           dma_channel, pio_get_dreq(pio1, MATRIX_SM, false),
           (unsigned long)overruns, (unsigned long)event_overruns);
}

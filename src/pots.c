#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "pots.h"
#include "pvs_config.h"

#define POT_RING_COUNT 64u
#define POT_DMA_COUNT 0x0fffffc0u
static uint16_t history[POT_RING_COUNT] __attribute__((aligned(128)));
static int dma_channel;
static uint16_t last1, last2;

void pots_init(void) {
    adc_init();
    adc_gpio_init(PVS_POT1_GPIO);
    adc_gpio_init(PVS_POT2_GPIO);
    adc_select_input(PVS_POT1_ADC);
    adc_set_round_robin((1u << PVS_POT1_ADC) | (1u << PVS_POT2_ADC));
    adc_fifo_setup(true, true, 1, false, false);
    adc_set_clkdiv(23999.0f); /* 48 MHz / 24000: 1 kHz per channel. */
    dma_channel = dma_claim_unused_channel(true);
    dma_channel_config config = dma_channel_get_default_config(dma_channel);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_16);
    channel_config_set_read_increment(&config, false);
    channel_config_set_write_increment(&config, true);
    channel_config_set_ring(&config, true, 7);
    channel_config_set_dreq(&config, DREQ_ADC);
    dma_channel_configure(dma_channel, &config, history, &adc_hw->fifo,
                          POT_DMA_COUNT, true);
    adc_run(true);
    absolute_time_t deadline = make_timeout_time_ms(20);
    while (POT_DMA_COUNT - dma_channel_hw_addr(dma_channel)->transfer_count < 16u &&
           !time_reached(deadline)) tight_loop_contents();
}

static uint16_t average(unsigned channel, uint32_t completed) {
    uint32_t last = completed - 1u;
    uint32_t index = last - ((last - channel) & 1u);
    uint32_t sum = 0;
    for (unsigned i = 0; i < 8; ++i, index -= 2u)
        sum += history[index & (POT_RING_COUNT - 1u)] & 0xfffu;
    uint16_t raw = (uint16_t)(sum / 8u);
    return (uint16_t)((raw << 4u) | (raw >> 8u));
}

void pots_read(uint16_t *p1, uint16_t *p2) {
    uint32_t completed = POT_DMA_COUNT - dma_channel_hw_addr(dma_channel)->transfer_count;
    __compiler_memory_barrier();
    if (completed >= 16u) {
        last1 = average(0, completed);
        last2 = average(1, completed);
    }
    *p1 = last1;
    *p2 = last2;
    if (completed == POT_DMA_COUNT) {
        /* Stop/restart ADC to preserve ADC0/ADC1 alignment after days of use. */
        adc_run(false);
        while (!(adc_hw->cs & ADC_CS_READY_BITS)) tight_loop_contents();
        adc_fifo_drain();
        adc_select_input(PVS_POT1_ADC);
        dma_channel_set_write_addr(dma_channel, history, false);
        dma_channel_set_trans_count(dma_channel, POT_DMA_COUNT, true);
        adc_run(true);
    }
}

void pots_print_status(void) {
    printf("Pots GP26/27 ADC0/1: DMA%d DREQ%d, 1000Hz/channel, average8, pickup800/65535\n",
           dma_channel, DREQ_ADC);
}

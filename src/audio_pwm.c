#include "audio_pwm.h"
#include "audio_rate.h"
#include "pvs_config.h"
#include "sam_engine.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"


/* GP0 owns PWM slice 0 (both compare halves) and two claimed DMA channels.
 * A claimed fractional DMA timer paces samples independently of the PWM
 * carrier. DMA_IRQ_0 hands off blocks; no per-sample CPU interrupt is used.
 * Halfword peripheral writes replicate into both halves of CC; only GP0 is
 * routed to PWM. Do not share this slice with another output. */
#define AUDIO_BLOCK_SAMPLES 2048u
static uint16_t blocks[2][AUDIO_BLOCK_SAMPLES] __attribute__((aligned(4)));
static unsigned pwm_slice, sample_timer;
static uint32_t configured_clock;
static int channels[2];
static unsigned filling, next_play;
static size_t used;
static volatile bool queued[2];
/* Hardware BUSY can clear before its completion IRQ is serviced. */
static volatile int active_channel = -1;
static volatile bool started, stopped = true;
static uint8_t depth = 8;
static uint32_t rate = PVS_DEFAULT_RATE, restarts;

static void (*service_callback)(void *);
static void *service_context;

static uint32_t channel_mask(void) {
    return (1u << channels[0]) | (1u << channels[1]);
}

/* Descriptors chain to themselves so a completed buffer cannot replay
 * while the renderer refills it. The highest-priority completion IRQ arms
 * the next ready block before the next sample timer request. */
static void start_queued(void) {
    if (stopped || active_channel >= 0 || !queued[next_play] || dma_channel_is_busy(channels[0]) ||
        dma_channel_is_busy(channels[1])) return;
    active_channel = (int)next_play;
    dma_start_channel_mask(1u << channels[next_play]);
    started = true;
}

static void reap(void) {
    uint32_t complete = dma_hw->ints0 & channel_mask();
    dma_hw->ints0 = complete;
    bool retired = false;
    for (unsigned i = 0; i < 2; ++i) {
        unsigned current = next_play;
        if (!(complete & (1u << channels[current]))) break;
        if (active_channel != (int)current) break;
        active_channel = -1;
        queued[current] = false;
        next_play ^= 1u;
        retired = true;
    }
    if (retired && started) start_queued();
}

static void dma_complete(void) { reap(); }

static void service(void) {
    if (service_callback) service_callback(service_context);
    tight_loop_contents();
}

void audio_pwm_set_service_callback(void (*callback)(void *), void *context) {
    service_callback = callback;
    service_context = context;
}

void audio_pwm_set_rate(uint32_t selected) {
    if (selected < PVS_MIN_RATE) selected = PVS_MIN_RATE;
    if (selected > PVS_MAX_RATE) selected = PVS_MAX_RATE;
    uint32_t clock = clock_get_hz(clk_sys);
    if (rate == selected && configured_clock == clock) return;
    uint16_t best_n, best_d;
    audio_rate_fraction(clock, selected, &best_n, &best_d);
    dma_timer_set_fraction(sample_timer, best_n, best_d);
    rate = selected;
    configured_clock = clock;
}

void audio_pwm_init(uint32_t selected) {
    pwm_slice = pwm_gpio_to_slice_num(PVS_AUDIO_PIN);
    gpio_set_function(PVS_AUDIO_PIN, GPIO_FUNC_PWM);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_wrap(&config, 255);
    pwm_config_set_clkdiv(&config, 1.0f);
    pwm_init(pwm_slice, &config, false);
    pwm_set_both_levels(pwm_slice, 128, 128);
    pwm_set_enabled(pwm_slice, true);
    sample_timer = (unsigned)dma_claim_unused_timer(true);

    for (unsigned i = 0; i < 2; ++i) channels[i] = dma_claim_unused_channel(true);
    for (unsigned i = 0; i < 2; ++i) {
        dma_channel_config dma = dma_channel_get_default_config(channels[i]);
        channel_config_set_transfer_data_size(&dma, DMA_SIZE_16);
        channel_config_set_read_increment(&dma, true);
        channel_config_set_write_increment(&dma, false);
        channel_config_set_dreq(&dma, dma_get_timer_dreq(sample_timer));
        channel_config_set_chain_to(&dma, channels[i]);
        channel_config_set_high_priority(&dma, true);
        dma_channel_configure(channels[i], &dma, &pwm_hw->slice[pwm_slice].cc, blocks[i], 0, false);
        dma_channel_set_irq0_enabled(channels[i], true);
    }
    irq_add_shared_handler(DMA_IRQ_0, dma_complete, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_priority(DMA_IRQ_0, PICO_HIGHEST_IRQ_PRIORITY);
    irq_set_enabled(DMA_IRQ_0, true);
    audio_pwm_set_rate(selected);
    audio_pwm_stop();
}

void audio_pwm_stop(void) {
    uint32_t irq = save_and_disable_interrupts();
    stopped = true;
    /* Prevent any IRQ handoff while aborting and clearing descriptors. */
    for (unsigned i = 0; i < 2; ++i)
        hw_clear_bits(&dma_channel_hw_addr(channels[i])->ctrl_trig, DMA_CH0_CTRL_TRIG_EN_BITS);
    for (unsigned i = 0; i < 2; ++i) {
        dma_channel_abort(channels[i]);
        dma_channel_set_trans_count(channels[i], 0, false);
        queued[i] = false;
    }
    dma_hw->ints0 = channel_mask();
    active_channel = -1;
    /* Keep a stable midpoint carrier between words instead of stepping
     * the filtered output from its DC bias to ground on every stop. */
    pwm_set_both_levels(pwm_slice, 128, 128);
    restore_interrupts(irq);
}

void audio_pwm_begin(uint8_t bits) {
    audio_pwm_stop();
    depth = bits < 1 ? 1 : bits > 8 ? 8 : bits;
    filling = next_play = 0;
    used = 0;
    started = stopped = false;
    for (unsigned i = 0; i < 2; ++i)
        hw_set_bits(&dma_channel_hw_addr(channels[i])->ctrl_trig, DMA_CH0_CTRL_TRIG_EN_BITS);
}

static void start_if_needed(bool flush) {
    uint32_t irq = save_and_disable_interrupts();
    reap();
    if (active_channel < 0 && !dma_channel_is_busy(channels[0]) && !dma_channel_is_busy(channels[1]) &&
        queued[next_play] && (started || flush || (queued[0] && queued[1]))) {
        if (started) ++restarts;
        start_queued();
    }
    restore_interrupts(irq);
}

static bool submit(bool flush) {
    if (!used) { start_if_needed(flush); return !stopped; }
    uint32_t irq = save_and_disable_interrupts();
    reap();
    dma_channel_set_read_addr(channels[filling], blocks[filling], false);
    dma_channel_set_trans_count(channels[filling], (uint32_t)used, false);
    queued[filling] = true;
    restore_interrupts(irq);
    filling ^= 1u;
    used = 0;
    start_if_needed(flush);
    return !stopped;
}

bool audio_pwm_write(const uint8_t *pcm, size_t count) {
    if (!pcm || stopped) return false;
    while (count) {
        while (queued[filling] && !stopped) {
            start_if_needed(false);
            service();
        }
        if (stopped) return false;
        size_t n = AUDIO_BLOCK_SAMPLES - used;
        if (n > count) n = count;
        for (size_t i = 0; i < n; ++i) blocks[filling][used + i] = sam_quantize(pcm[i], depth);
        used += n;
        pcm += n;
        count -= n;
        if (used == AUDIO_BLOCK_SAMPLES && !submit(false)) return false;
        service();
    }
    return !stopped;
}

bool audio_pwm_end(void) {
    if (stopped || !submit(true)) return false;
    while ((queued[0] || queued[1]) && !stopped) {
        start_if_needed(true);
        service();
    }
    if (stopped) return false;
    /* DMA completion writes the final shadow compare value. Allow one
     * full sample plus a PWM period for it to latch and play before idling. */
    absolute_time_t drained = make_timeout_time_us((1000000u + PVS_MIN_RATE - 1u) / PVS_MIN_RATE + 3u);
    while (!time_reached(drained) && !stopped) service();
    if (stopped) return false;
    audio_pwm_stop();
    return true;
}

void audio_pwm_print_status(void) {
    printf("Audio GP0: upstream C SAM, PWM slice%u, DMA%d/%d timer%u DREQ%u IRQ0, %luHz, carrier%luHz, %luB buffers, restarts%lu\n",
           pwm_slice, channels[0], channels[1], sample_timer, dma_get_timer_dreq(sample_timer),
           (unsigned long)rate, (unsigned long)(clock_get_hz(clk_sys) / 256u),
           (unsigned long)sizeof blocks, (unsigned long)restarts);
}

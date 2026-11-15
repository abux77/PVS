#include "audio_pwm.h"
#include "pvs_config.h"
#include "sam_engine.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "audio_pwm.pio.h"

/* Audio owns PIO0 SM0 and two SDK-claimed DMA channels. DMA_IRQ_0 uses a
 * shared handler which acknowledges only those channels. All PCM lives in RAM.
 * The renderer fills one buffer while the other feeds the PIO FIFO. */
#define AUDIO_SM 0u
#define AUDIO_BLOCK_BYTES 1024u
static uint8_t blocks[2][AUDIO_BLOCK_BYTES] __attribute__((aligned(4)));
static int channels[2];
static unsigned program_offset, filling, next_play;
static size_t used;
static volatile bool queued[2];
/* Hardware BUSY can clear before its completion IRQ is serviced. */
static volatile int active_channel = -1;
static volatile bool started, stopped = true;
static uint8_t depth = 8;
static uint32_t rate = PVS_DEFAULT_RATE, restarts;
static volatile uint32_t underruns;
static void (*service_callback)(void *);
static void *service_context;

static uint32_t channel_mask(void) {
    return (1u << channels[0]) | (1u << channels[1]);
}

/* Called only by our IRQ or with interrupts disabled. Software advances the
 * DMA descriptors: hardware chaining would reload a retired channel's old
 * transfer count and could replay its block while the renderer refills it.
 * The joined PIO FIFO holds 32 samples (>700 us at maximum rate), covering the
 * short completion-IRQ handoff between blocks. */
static void start_queued(void) {
    if (stopped || active_channel >= 0 || !queued[next_play] || dma_channel_is_busy(channels[0]) ||
        dma_channel_is_busy(channels[1])) return;
    /* TXSTALL is sticky: count a starvation only when resuming an active
     * stream. Initial startup and the intentional final drain are excluded. */
    uint32_t stall = 1u << (PIO_FDEBUG_TXSTALL_LSB + AUDIO_SM);
    if (started && (pio0->fdebug & stall)) ++underruns;
    pio0->fdebug = stall;
    active_channel = (int)next_play;
    dma_start_channel_mask(1u << channels[next_play]);
    pio_sm_set_enabled(pio0, AUDIO_SM, true);
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
    rate = selected;
    pio_sm_set_clkdiv(pio0, AUDIO_SM, (float)clock_get_hz(clk_sys) / ((float)rate * 770.0f));
}

void audio_pwm_init(uint32_t selected) {
    pio_sm_claim(pio0, AUDIO_SM);
    program_offset = pio_add_program(pio0, &pvs_audio_pwm_program);
    pio_gpio_init(pio0, PVS_AUDIO_PIN);
    pio_sm_config config = pvs_audio_pwm_program_get_default_config(program_offset);
    sm_config_set_sideset_pins(&config, PVS_AUDIO_PIN);
    sm_config_set_out_shift(&config, true, true, 32);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
    pio_sm_init(pio0, AUDIO_SM, program_offset, &config);
    pio_sm_set_consecutive_pindirs(pio0, AUDIO_SM, PVS_AUDIO_PIN, 1, true);
    for (unsigned i = 0; i < 2; ++i) channels[i] = dma_claim_unused_channel(true);
    for (unsigned i = 0; i < 2; ++i) {
        dma_channel_config dma = dma_channel_get_default_config(channels[i]);
        channel_config_set_transfer_data_size(&dma, DMA_SIZE_32);
        channel_config_set_read_increment(&dma, true);
        channel_config_set_write_increment(&dma, false);
        channel_config_set_dreq(&dma, pio_get_dreq(pio0, AUDIO_SM, true));
        channel_config_set_chain_to(&dma, channels[i]);
        channel_config_set_high_priority(&dma, true);
        dma_channel_configure(channels[i], &dma, &pio0->txf[AUDIO_SM], blocks[i], 0, false);
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
    pio_sm_set_enabled(pio0, AUDIO_SM, false);
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
    pio_sm_clear_fifos(pio0, AUDIO_SM);
    pio_sm_restart(pio0, AUDIO_SM);
    pio_sm_set_pins_with_mask(pio0, AUDIO_SM, 0, 1u << PVS_AUDIO_PIN);
    restore_interrupts(irq);
}

void audio_pwm_begin(uint8_t bits) {
    audio_pwm_stop();
    depth = bits < 1 ? 1 : bits > 8 ? 8 : bits;
    filling = next_play = 0;
    used = 0;
    started = stopped = false;
    /* PIO's ISR is the fixed PWM counter, and OSR starts empty for autopull. */
    pio_sm_put(pio0, AUDIO_SM, 255);
    pio_sm_exec(pio0, AUDIO_SM, pio_encode_pull(false, false));
    /* SM_RESTART empties the output shift counter but also CLEARS ISR.
     * OSR survives, so copy the PWM limit only after restarting. */
    pio_sm_restart(pio0, AUDIO_SM);
    pio_sm_exec(pio0, AUDIO_SM, pio_encode_mov(pio_isr, pio_osr));
    pio_sm_exec(pio0, AUDIO_SM, pio_encode_jmp(program_offset));
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
    while (used & 3u) blocks[filling][used++] = 128;
    uint32_t irq = save_and_disable_interrupts();
    reap();
    dma_channel_set_read_addr(channels[filling], blocks[filling], false);
    dma_channel_set_trans_count(channels[filling], (uint32_t)used / 4u, false);
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
        size_t n = AUDIO_BLOCK_BYTES - used;
        if (n > count) n = count;
        for (size_t i = 0; i < n; ++i) blocks[filling][used + i] = sam_quantize(pcm[i], depth);
        used += n;
        pcm += n;
        count -= n;
        if (used == AUDIO_BLOCK_BYTES && !submit(false)) return false;
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
    while (!pio_sm_is_tx_fifo_empty(pio0, AUDIO_SM) && !stopped) service();
    if (stopped) return false;
    /* An empty FIFO may still leave four samples in OSR. Use the minimum
     * allowed rate because the live callback can slow playback while draining. */
    absolute_time_t drained = make_timeout_time_us((4000000u + PVS_MIN_RATE - 1u) / PVS_MIN_RATE);
    while (!time_reached(drained) && !stopped) service();
    if (stopped) return false;
    audio_pwm_stop();
    return true;
}

void audio_pwm_print_status(void) {
    printf("Audio GP0: upstream C SAM, PIO0 SM0 DMA%d/%d DREQ%u IRQ0, %luHz, 2048B PCM, restarts%lu\n",
           channels[0], channels[1], pio_get_dreq(pio0, AUDIO_SM, true),
           (unsigned long)rate, (unsigned long)restarts);
    printf("Audio FIFO underruns: %lu (since boot)\n", (unsigned long)underruns);
}

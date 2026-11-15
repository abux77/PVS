#pragma once
#include <stdint.h>

/* Fractional DMA timer: sample Hz = system clock * numerator / denominator. */
static inline void audio_rate_fraction(uint32_t clock, uint32_t selected,
                                       uint16_t *numerator, uint16_t *denominator) {
    /* Find the closest representable X/Y rate. At Pico clocks and our sample
     * rates there are fewer than 50 candidate numerators, so live changes are
     * bounded and inexpensive. Compare frequency errors without floats. */
    uint32_t best_n = 1, best_d = 1;
    uint64_t best_error = UINT64_MAX;
    uint32_t limit = ((uint64_t)selected * 65535u + clock / 2u) / clock;
    if (!limit) limit = 1;
    for (uint32_t n = 1; n <= limit; ++n) {
        uint32_t d = ((uint64_t)clock * n + selected / 2u) / selected;
        if (!d || d > 65535u) continue;
        int64_t delta = (int64_t)clock * n - (int64_t)selected * d;
        uint64_t error = delta < 0 ? (uint64_t)-delta : (uint64_t)delta;
        if (best_error == UINT64_MAX || error * best_d < best_error * d) {
            best_error = error;
            best_n = n;
            best_d = d;
        }
    }
    *numerator = (uint16_t)best_n;
    *denominator = (uint16_t)best_d;
}

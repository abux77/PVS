#include "audio_rate.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    const uint32_t clocks[] = {125000000, 150000000, 250000000};
    for (unsigned i = 0; i < sizeof clocks / sizeof clocks[0]; ++i) {
        for (uint32_t rate = 8000; rate <= 88200; ++rate) {
            uint16_t n, d;
            audio_rate_fraction(clocks[i], rate, &n, &d);
            assert(n && d && n < d);
            int64_t error = (int64_t)clocks[i] * n - (int64_t)rate * d;
            if (error < 0) error = -error;
            /* Every selectable rate is within 1 Hz on each supported clock. */
            assert(error < d);
        }
    }
    puts("DMA sample-rate fractions pass across the full live range");
}

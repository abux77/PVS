#include "pvs_memory.h"
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* Pico SDK newlib uses __StackLimit as the upper heap boundary. The linker
 * reserves the core stacks outside this range. Do not count stack space as
 * available heap, and do not probe RAM by writing into unallocated memory. */
extern char __StackLimit;
extern void *_sbrk(int increment);
#define SAMPLE_LIMIT (128u * 1024u)
#define HEAP_HEADROOM (32u * 1024u)
static uint8_t *sample_buffer;
static size_t sample_capacity, boot_available;
static int initialized;

static size_t unclaimed_heap(void) {
    void *current = _sbrk(0);
    uintptr_t top = (uintptr_t)&__StackLimit;
    uintptr_t bottom = (uintptr_t)current;
    return current == (void *)-1 || bottom >= top ? 0 : top - bottom;
}

void pvs_memory_init(void) {
    if (initialized) return;
    initialized = 1;
    /* Base the reservation on untouched contiguous heap, not potentially
     * fragmented allocator free blocks. Leave room for malloc bookkeeping. */
    boot_available = unclaimed_heap();
    size_t wanted = boot_available > HEAP_HEADROOM + 1024u ?
        (boot_available - HEAP_HEADROOM - 1024u) & ~(size_t)1023u : 0;
    if (wanted > SAMPLE_LIMIT) wanted = SAMPLE_LIMIT;
    while (wanted) {
        sample_buffer = malloc(wanted);
        if (sample_buffer) {
            sample_capacity = wanted;
            break;
        }
        wanted /= 2u;
    }
}

uint8_t *pvs_sample_buffer(size_t *capacity) {
    if (capacity) *capacity = sample_capacity;
    return sample_buffer;
}

void pvs_memory_print_status(void) {
    struct mallinfo info = mallinfo();
    size_t unclaimed = unclaimed_heap();
    size_t reusable = info.fordblks > 0 ? (size_t)info.fordblks : 0;
    printf("RAM heap: %luB available (%luB unclaimed + %luB allocator free); stacks excluded\n",
           (unsigned long)(unclaimed + reusable), (unsigned long)unclaimed,
           (unsigned long)reusable);
    printf("Sample reserve: %luB (%lums at 22050Hz, 8-bit); raw PCM cache; boot heap %luB\n",
           (unsigned long)sample_capacity,
           (unsigned long)(sample_capacity * 1000u / 22050u),
           (unsigned long)boot_available);
}

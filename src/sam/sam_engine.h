#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Static workspace; one renderer at a time. User input is rejected on overflow. */
#define SAM_TEXT_CAPACITY 384
#define SAM_PHONEME_CAPACITY 1024
#define SAM_FRAME_CAPACITY 4096

typedef struct {
    uint8_t index[SAM_PHONEME_CAPACITY];
    uint8_t stress[SAM_PHONEME_CAPACITY];
    uint8_t length[SAM_PHONEME_CAPACITY];
    size_t count;
} sam_phonemes_t;
typedef bool (*sam_pcm_sink_t)(const uint8_t *samples, size_t count, void *context);

bool sam_recite(const char *text, char *output, size_t capacity);
bool sam_parse(const char *text, sam_phonemes_t *phonemes);
/* Preserve the reference reciter's legacy exception spellings, which sometimes
 * contain a symbol skipped by its Python parser (e.g. HOTEL's leading H). */
bool sam_parse_recited(const char *text, sam_phonemes_t *phonemes);
bool sam_render(const sam_phonemes_t *phonemes, uint8_t speed, uint8_t pitch,
                uint8_t mouth, uint8_t throat, sam_pcm_sink_t sink, void *context);
uint8_t sam_quantize(uint8_t sample, uint8_t bits);

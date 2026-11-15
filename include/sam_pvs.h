#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { uint8_t speed,pitch,mouth,throat; uint32_t sample_rate; uint8_t bit_depth; } sam_voice_t;
void sam_pvs_init(void);
void sam_pvs_set_voice(const sam_voice_t *voice);
void sam_pvs_set_live_rate(uint32_t sample_rate);
void sam_pvs_set_service_callback(void (*callback)(void *), void *context);
void sam_pvs_set_word_callback(void (*callback)(void *), void *context);
bool sam_pvs_say(const char *text);
bool sam_pvs_say_phonetic(const char *phonemes);
bool sam_pvs_is_busy(void);
void sam_pvs_stop(void);
void sam_pvs_print_status(void);

/* Completed cached segments reused since boot (diagnostics/tests). */
unsigned sam_pvs_cache_hits(void);

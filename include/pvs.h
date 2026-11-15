#pragma once
#include <stdbool.h>
#include "sam_pvs.h"
/* Parameter bits: speed, pitch, mouth, throat, sample rate, bit depth. */
typedef struct {
    sam_voice_t voice;
    uint8_t hardware_mask, lock_mask, mode;
    bool busy, editable;
} pvs_controls_t;
void pvs_controls_read(pvs_controls_t *state);
/* Core 0 only. Accepted edits are session values, not automatic flash saves. */
const char *pvs_controls_set(const char *parameter, uint32_t value);
/* Core 0 only; MIDI channel filtering lives in midi.c. */
void pvs_midi_cc(uint8_t controller, uint8_t value);
void pvs_midi_note(uint8_t note, uint8_t velocity);
typedef struct { uint32_t revision; unsigned bank; int playing; } pvs_bank_state_t;
typedef struct {
    uint32_t revision;
    uint16_t length;
    uint8_t bank, locks, count;
    char kind; /* S=text, P=phonetic, E=unassigned. */
    uint8_t text[48];
} pvs_phrase_chunk_t;
void pvs_bank_read(pvs_bank_state_t *state);
bool pvs_phrase_chunk(unsigned key, unsigned chunk, pvs_phrase_chunk_t *out);
const char *pvs_trigger_key(unsigned key);
void pvs_init(void);
void pvs_task(void);
void pvs_print_status(void);
/* Reload user-edited startup state, banks, locks and presets after USB eject. */
void pvs_reload_storage(void);

void pvs_request_bootsel(void);
bool pvs_bootsel_ready(void);

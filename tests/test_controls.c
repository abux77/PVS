/* Behavioral integration tests for the production pvs.c state machine.
 * Each named scenario runs in a fresh process, as a fresh firmware boot. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pvs.h"
#include "pvs_config.h"
#include "matrix.h"
#include "pots.h"
#include "storage.h"
#include "sam_pvs.h"
#include "ws2812.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
typedef struct {
    char text[128];
    bool phonetic;
    sam_voice_t voice;
    uint32_t rate_after_service;
    uint8_t bits_after_service;
} spoken_word_t;
static spoken_word_t spoken[128];
static unsigned spoken_count, speech_calls, phrase_loads, preset_writes, lock_writes, startup_writes;
static unsigned matrix_services, storage_services, storage_initializations;
static uint16_t raw1 = 32768, raw2 = 32768;
static matrix_state_t matrix_event;
static uint32_t now_ms = 100;
static uint8_t leds[PVS_LED_COUNT][3];
static sam_voice_t rendered_voice;
static bool audio_busy;
static void (*word_callback)(void *), (*service_callback)(void *);
static void *word_context, *service_context;
static void (*during_word)(unsigned word_index);
static unsigned within_speech_word;
static sam_voice_t disk_presets[12];
static uint16_t disk_preset_mask;
static phrase_lock_t disk_locks[12][12];
static unsigned disk_bank = 1, disk_preset;
static bool disk_bank_available[12];
static bool edit_request, edit_active, reload_ready;
static bool format_request;
static unsigned formats;
static unsigned edit_entries;
static void (*status_callback)(void);
static unsigned mock_phrase_count=12;
static char bank_one_text[PVS_MAX_PHRASE] = "hello world";

static sam_voice_t voice_values(uint8_t speed, uint8_t pitch, uint8_t mouth,
                                uint8_t throat, uint32_t rate, uint8_t bits) {
    return (sam_voice_t){speed, pitch, mouth, throat, rate, bits};
}

void matrix_init(void) {}
void matrix_task(void) { ++matrix_services; }
void matrix_print_status(void) {}
matrix_state_t matrix_scan(void) {
    matrix_state_t result = matrix_event;
    result.time_ms = now_ms;
    matrix_event.pressed = matrix_event.released = 0;
    return result;
}
void pots_init(void) {}
void pots_read(uint16_t *p1, uint16_t *p2) { *p1 = raw1; *p2 = raw2; }
void pots_print_status(void) {}
void ws2812_init(void) {}
void ws2812_set(unsigned i, uint8_t r, uint8_t g, uint8_t b) {
    assert(i < PVS_LED_COUNT);
    leds[i][0] = r; leds[i][1] = g; leds[i][2] = b;
}
void ws2812_clear(void) { memset(leds, 0, sizeof leds); }
void ws2812_show(void) {}
void ws2812_task(void) {}
void sam_pvs_init(void) {}
void sam_pvs_set_voice(const sam_voice_t *voice) { rendered_voice = *voice; }
void sam_pvs_set_live_rate(uint32_t rate) { rendered_voice.sample_rate = rate; }
void sam_pvs_set_service_callback(void (*callback)(void *), void *context) {
    service_callback = callback; service_context = context;
}
void sam_pvs_set_word_callback(void (*callback)(void *), void *context) {
    word_callback = callback; word_context = context;
}
bool sam_pvs_is_busy(void) { return audio_busy; }
void sam_pvs_stop(void) { audio_busy = false; }
void sam_pvs_print_status(void) {}
static bool mock_speech(const char *text, bool phonetic) {
    assert(!audio_busy);
    audio_busy = true;
    ++speech_calls;
    within_speech_word = 0;
    while (*text && audio_busy) {
        while (*text == ' ') ++text;
        if (!*text) break;
        const char *start = text;
        while (*text && *text != ' ') ++text;
        assert(spoken_count < ARRAY_SIZE(spoken));
        spoken_word_t *word = &spoken[spoken_count++];
        size_t size = (size_t)(text - start);
        assert(size < sizeof word->text);
        memcpy(word->text, start, size);
        word->text[size] = 0;
        word->phonetic = phonetic;
        assert(word_callback && service_callback);
        word_callback(word_context);
        word->voice = rendered_voice;
        if (during_word) during_word(within_speech_word);
        service_callback(service_context);
        word->rate_after_service = rendered_voice.sample_rate;
        word->bits_after_service = rendered_voice.bit_depth;
        ++within_speech_word;
    }
    audio_busy = false;
    return true;
}
bool sam_pvs_say(const char *text) { return mock_speech(text, false); }
bool sam_pvs_say_phonetic(const char *text) { return mock_speech(text, true); }

bool storage_init(void) { ++storage_initializations; return true; }
void storage_set_status_callback(void (*callback)(void)) { status_callback = callback; }
bool storage_is_ready(void) { return !edit_active; }
bool storage_load_bank(unsigned bank, phrase_bank_t *out) {
    assert(!audio_busy && !edit_active);
    if (bank < 1 || bank > 12 || !disk_bank_available[bank - 1]) return false;
    ++phrase_loads;
    memset(out, 0, sizeof *out);
    out->count = mock_phrase_count;
    for (unsigned i = 0; i < 12; ++i) {
        out->item[i].kind = PHRASE_SAY;
        snprintf(out->item[i].text, sizeof out->item[i].text, "bank%u phrase%u", bank, i + 1);
    }
    if (bank == 1) snprintf(out->item[0].text, sizeof out->item[0].text, "%s", bank_one_text);
    out->item[1].kind = PHRASE_PHONETIC;
    snprintf(out->item[1].text, sizeof out->item[1].text, "/HEH4LOW WER4LD");
    return true;
}
bool storage_load_presets(sam_voice_t out[12], uint16_t *mask) {
    assert(!audio_busy && !edit_active);
    memcpy(out, disk_presets, sizeof disk_presets); *mask = disk_preset_mask; return true;
}
bool storage_save_presets(const sam_voice_t in[12], uint16_t mask) {
    assert(!audio_busy && !edit_active);
    memcpy(disk_presets, in, sizeof disk_presets); disk_preset_mask = mask; ++preset_writes; return true;
}
bool storage_load_locks(unsigned bank, phrase_lock_t out[12]) {
    assert(!audio_busy && !edit_active && bank >= 1 && bank <= 12);
    memcpy(out, disk_locks[bank - 1], sizeof disk_locks[0]); return true;
}
bool storage_save_locks(unsigned bank, const phrase_lock_t in[12]) {
    assert(!audio_busy && !edit_active && bank >= 1 && bank <= 12);
    memcpy(disk_locks[bank - 1], in, sizeof disk_locks[0]); ++lock_writes; return true;
}
bool storage_load_startup(unsigned *bank, unsigned *preset) {
    assert(!audio_busy && !edit_active); *bank = disk_bank; *preset = disk_preset; return true;
}
bool storage_save_startup(unsigned bank, unsigned preset) {
    assert(!audio_busy && !edit_active); disk_bank = bank; disk_preset = preset; ++startup_writes; return true;
}
void storage_console_service(void) { ++storage_services; }
bool storage_edit_requested(void) { bool result = edit_request; edit_request = false; return result; }
bool storage_format_requested(void) { bool result = format_request; format_request = false; return result; }
bool storage_format_factory(void) {
    assert(!audio_busy);
    if (edit_active) return false;
    ++formats; reload_ready = true; return true;
}
bool storage_begin_edit(void) { assert(!audio_busy); ++edit_entries; edit_active = true; return true; }
bool storage_edit_active(void) { return edit_active; }
bool storage_reload_pending(void) { bool result = reload_ready; reload_ready = false; return result; }

static void boot(void) {
    for (unsigned i = 0; i < 12; ++i) disk_bank_available[i] = true;
    pvs_init();
    assert(storage_initializations == 1 && status_callback);
}
uint64_t test_time_us;
static char screen[3][32];
static void tick(uint32_t elapsed) { now_ms += elapsed; test_time_us=(uint64_t)now_ms*1000u; pvs_task(); }
static void event(uint16_t pressed, uint16_t released, uint32_t elapsed) {
    now_ms += elapsed;
    test_time_us=(uint64_t)now_ms*1000u;
    matrix_event.down = (uint16_t)((matrix_event.down | pressed) & ~released);
    matrix_event.pressed = pressed; matrix_event.released = released;
    pvs_task();
}
static void press(unsigned button) { assert(button >= 1 && button <= 16); event((uint16_t)(1u << (button - 1u)), 0, 20); }
static void release(unsigned button) { event(0, (uint16_t)(1u << (button - 1u)), 20); }
static void tap(unsigned button) { press(button); release(button); }
static void clear_spoken(void) { spoken_count = speech_calls = 0; }
static void pair(unsigned first, uint8_t r, uint8_t g, uint8_t b) {
    for (unsigned i = first; i < first + 2; ++i) {
        assert(leds[i][0] == r && leds[i][1] == g && leds[i][2] == b);
    }
}
static void select_bank(unsigned bank) { press(16); tap(bank); release(16); }
static void lock_short(unsigned modifier, unsigned phrase) { press(modifier); tap(phrase); release(modifier); }

static void test_startup(void) {
    disk_bank = 2; disk_preset = 3; disk_preset_mask = 1u << 2;
    disk_presets[2] = voice_values(80, 90, 100, 110, 16000, 5);
    boot();
    assert(rendered_voice.speed == 80 && rendered_voice.pitch == 90);
    assert(rendered_voice.mouth == 100 && rendered_voice.throat == 110);
    assert(rendered_voice.sample_rate == 16000 && rendered_voice.bit_depth == 5);
    assert(startup_writes == 0 && speech_calls == 0);
    tap(1);
    assert(spoken_count == 2 && !strcmp(spoken[0].text, "bank2"));
    assert(spoken[0].voice.speed == 80 && spoken[0].voice.pitch == 90);
    assert(spoken[0].voice.sample_rate == 16000 && spoken[0].voice.bit_depth == 5);
}

static void test_modes_pickup(void) {
    boot();
    tap(1);
    assert(spoken[0].voice.speed == 72 && spoken[0].voice.pitch == 96);
    clear_spoken();
    raw1 += 799; raw2 -= 799; tap(1);
    assert(spoken[0].voice.speed == 72 && spoken[0].voice.pitch == 96);
    clear_spoken();
    raw1 = 65535; raw2 = 0; tap(1);
    assert(spoken[0].voice.speed == 1 && spoken[0].voice.pitch == 10);
    tap(13);
    pair(6, 255, 255, 255);
    clear_spoken(); tap(1);
    assert(spoken[0].voice.mouth == 128 && spoken[0].voice.throat == 128);
    raw1 = 0; raw2 = 65535;
    clear_spoken(); tap(1);
    assert(spoken[0].voice.mouth == 255 && spoken[0].voice.throat == 1);
    tap(14); /* Sample replaces Voice. */
    pair(0, 0, 0, 255); pair(6, 0, 0, 0);
    raw1 = 65535; raw2 = 0;
    clear_spoken(); tap(1);
    assert(spoken[0].voice.sample_rate == 8000 && spoken[0].voice.bit_depth == 1);
    assert(spoken[0].voice.mouth == 255 && spoken[0].voice.throat == 1);
    tap(15); /* Pitch replaces Sample; retained rate does not follow pot1. */
    pair(0, 0, 0, 0); pair(4, 255, 0, 0); pair(6, 0, 0, 0);
    raw1 = 0; raw2 = 65535;
    clear_spoken(); tap(1);
    assert(spoken[0].voice.sample_rate == 8000 && spoken[0].voice.bit_depth == 1);
    assert(spoken[0].voice.pitch == 255 && spoken[0].voice.mouth == 255);
    tap(14); pair(0, 0, 0, 255); pair(4, 0, 0, 0);
    tap(14); pair(0, 0, 0, 0);
    clear_spoken(); tap(1);
    assert(spoken[0].voice.sample_rate == 8000 && spoken[0].voice.bit_depth == 1);
}

static void test_pickup_threshold(void) {
    boot();
    raw1 = 32768 + 799; raw2 = 32768 - 799;
    tap(1); assert(spoken[0].voice.speed == 72 && spoken[0].voice.pitch == 96);
    clear_spoken();
    raw1 = 32768 + 800; raw2 = 32768 - 800;
    tap(1); assert(spoken[0].voice.speed == 124 && spoken[0].voice.pitch == 129);
    /* Once picked up, returning to the old position continues control. */
    clear_spoken(); raw1 = raw2 = 32768; tap(1);
    assert(spoken[0].voice.speed == 127 && spoken[0].voice.pitch == 132);
}

static void change_sample_during_first_word(unsigned word) {
    if (word == 0) { raw1 = 65535; raw2 = 0; }
}
static void test_live_modulation(void) {
    boot(); tap(14);
    during_word = change_sample_during_first_word;
    tap(1);
    assert(spoken_count == 2);
    assert(spoken[0].voice.sample_rate == 22050 && spoken[0].voice.bit_depth == 8);
    assert(spoken[0].rate_after_service == 8000 && spoken[0].bits_after_service == 8);
    assert(spoken[1].voice.sample_rate == 8000 && spoken[1].voice.bit_depth == 1);
    assert(matrix_services >= 2 && storage_services >= 2);
    during_word = NULL;
    tap(14);
    clear_spoken(); tap(1);
    assert(spoken[0].voice.sample_rate == 8000 && spoken[0].voice.bit_depth == 1);
}

static void change_voice_during_first_word(unsigned word) {
    if (word == 0) { raw1 = 65535; raw2 = 0; }
}
static void test_word_modulation(void) {
    boot(); during_word = change_voice_during_first_word; tap(1);
    assert(spoken[0].voice.speed == 72 && spoken[0].voice.pitch == 96);
    assert(spoken[1].voice.speed == 1 && spoken[1].voice.pitch == 10);
}

static void test_chromatic_last(void) {
    boot(); tap(15); tap(12); assert(spoken_count == 0); tap(15);
    tap(2); assert(spoken[0].phonetic && !strcmp(spoken[0].text, "/HEH4LOW"));
    select_bank(2);
    tap(15);
    clear_spoken(); tap(12);
    assert(spoken[0].phonetic && !strcmp(spoken[0].text, "/HEH4LOW"));
    assert(spoken[0].voice.pitch == 51); /* round(96 * 0.529732) */
    assert(rendered_voice.pitch == 96);
    clear_spoken(); tap(12); assert(spoken[0].voice.pitch == 51);
    clear_spoken(); tap(1); assert(spoken[0].voice.pitch == 96);
    assert(disk_bank == 2 && startup_writes == 1);
}

static void test_presets(void) {
    disk_preset_mask = 1u << 1;
    disk_presets[1] = voice_values(77, 88, 99, 111, 12350, 4);
    boot();
    tap(16); pair(2, 0, 255, 0);
    tap(2); pair(2, 0, 0, 0);
    assert(disk_preset == 2 && startup_writes == 1);
    assert(spoken_count == 2 && !strcmp(spoken[0].text, "Preset") && !strcmp(spoken[1].text, "loaded"));
    assert(spoken[0].voice.speed == 77 && spoken[0].voice.bit_depth == 4);
    clear_spoken(); tap(1);
    assert(spoken[0].voice.pitch == 88); /* Recall re-arms both pots. */
    tap(16); press(3); tick(1999); assert(preset_writes == 0);
    tick(1); assert(preset_writes == 1 && (disk_preset_mask & (1u << 2)));
    assert(disk_presets[2].speed == 77 && disk_presets[2].sample_rate == 12350);
    assert(!strcmp(spoken[spoken_count - 1].text, "stored"));
    release(3); pair(2, 0, 0, 0);
    unsigned calls = speech_calls; tap(16); tap(12);
    assert(speech_calls == calls); pair(2, 0, 0, 0); /* Empty preset exits. */
    tap(16); tap(16); pair(2, 0, 0, 0); /* D cancels. */
}

static void test_locks(void) {
    boot();
    lock_short(15, 1); assert(disk_locks[0][0].flags == LOCK_PITCH);
    lock_short(13, 1); assert(disk_locks[0][0].flags == (LOCK_PITCH | LOCK_VOICE));
    lock_short(14, 1); assert(disk_locks[0][0].flags == 7 && lock_writes == 3);
    assert(speech_calls == 0); pair(0, 0, 0, 0); pair(4, 0, 0, 0); pair(6, 0, 0, 0);
    raw1 = 65535; raw2 = 0; tap(1);
    assert(spoken[0].voice.speed == 72 && spoken[0].voice.pitch == 96);
    assert(rendered_voice.speed == 1 && rendered_voice.pitch == 10); /* Base preflight survives lock. */
    tap(15); clear_spoken(); tap(12);
    assert(spoken[0].voice.pitch == 51); /* Chromatic offset uses locked base. */
    tap(15);
    press(15); press(1); tick(1999); assert(disk_locks[0][0].flags == 7);
    tick(1); assert(disk_locks[0][0].flags == (LOCK_VOICE | LOCK_SAMPLE));
    release(1); release(15); assert(lock_writes == 4); /* No store on long-hold release. */
    press(16); press(14); assert(disk_locks[0][0].flags == 0);
    release(14); release(16);
    pair(0, 0, 0, 0); pair(2, 0, 0, 0);
    assert(lock_writes == 5);
}

static void test_locked_sample(void) {
    disk_locks[0][0] = (phrase_lock_t){LOCK_SAMPLE, {72, 96, 128, 128, 16000, 3}};
    boot(); tap(14);
    during_word = change_sample_during_first_word; tap(1);
    assert(spoken[0].voice.sample_rate == 16000 && spoken[0].voice.bit_depth == 3);
    assert(spoken[0].rate_after_service == 16000 && spoken[1].voice.bit_depth == 3);
    assert(rendered_voice.sample_rate == 8000 && rendered_voice.bit_depth == 1); /* Idle pots resume after the locked phrase. */
    during_word = NULL;
    clear_spoken(); tap(2); /* Unlocked phrase picks up newly moved pots. */
    assert(spoken[0].voice.sample_rate == 8000 && spoken[0].voice.bit_depth == 1);
}

static void request_edit_during_word(unsigned word) { if (word == 0) edit_request = true; }
static void request_format_during_word(unsigned word) { if (word == 0) format_request = true; }
static void test_format(void) {
    boot(); during_word = request_format_during_word;
    press(1);
    assert(formats == 0 && format_request);
    during_word = NULL;
    tick(1); assert(formats == 1 && phrase_loads == 2 && !format_request);
    edit_active = true; format_request = true;
    tick(1); assert(formats == 1);
}
static void test_edit(void) {
    boot(); during_word = request_edit_during_word;
    press(1);
    assert(!edit_active && edit_entries == 0); /* Request only, throughout speech. */
    during_word = NULL;
    tick(1); assert(edit_active && edit_entries == 1);
    unsigned calls = speech_calls;
    release(1); tap(2); tap(14); assert(speech_calls == calls);
    snprintf(bank_one_text, sizeof bank_one_text, "edited phrase");
    edit_active = false; reload_ready = true;
    tick(1); assert(phrase_loads == 2);
    clear_spoken(); tap(1);
    assert(!strcmp(spoken[0].text, "edited"));
    pair(0, 0, 0, 0);
}

static void test_bank_priority(void) {
    boot(); press(16); press(14); tap(2); release(14); release(16);
    assert(disk_bank == 2 && lock_writes == 1); /* Clear all, then D bank priority. */
    assert(disk_locks[1][1].flags == 0 && speech_calls == 0);
    pair(0, 0, 0, 0); pair(2, 0, 0, 0);
    disk_bank_available[2] = false;
    select_bank(3); assert(disk_bank == 2);
    tap(1); assert(!strcmp(spoken[0].text, "bank2"));
}


static void retrigger_once(unsigned word) {
    if (word == 0) {
        during_word = NULL;
        /* Debounced release/repress has already occurred in scan history. */
        matrix_event.down = matrix_event.pressed = 1;
    }
}
static void switch_once(unsigned word) {
    if (word == 0) {
        during_word = NULL;
        matrix_event.down = matrix_event.pressed = 2;
    }
}
static void test_retrigger(void) {
    boot(); during_word = retrigger_once;
    press(1);
    assert(speech_calls == 2 && spoken_count == 3);
    assert(!strcmp(spoken[0].text, "hello"));
    assert(!strcmp(spoken[1].text, "hello") && !strcmp(spoken[2].text, "world"));
    release(1); tick(1);
    assert(speech_calls == 2); /* Retrigger press was consumed, not queued. */
    clear_spoken();
    unsigned previous_calls = speech_calls;
    during_word = switch_once;
    press(1);
    assert(speech_calls == previous_calls + 2 && spoken_count == 3);
    assert(!strcmp(spoken[0].text,"hello"));
    assert(!strcmp(spoken[1].text,"/HEH4LOW") && spoken[1].phonetic);
    assert(!strcmp(spoken[2].text,"WER4LD"));
    release(2); tick(1);
    assert(speech_calls == previous_calls + 2); /* New key was consumed too. */
}
static void chord_during_word(unsigned word) {
    if (word == 0) {
        during_word = NULL;
        matrix_event.down |= (1u << 12) | (1u << 15);
        matrix_event.pressed = (1u << 12) | (1u << 15);
    }
}
static void test_edit_chord(void) {
    boot(); press(13); press(16); tick(1);
    assert(edit_active && edit_entries == 1);
    release(13); release(16);
    edit_active = false; reload_ready = true; tick(1);
    pair(0, 0, 0, 0); pair(6, 0, 0, 0);
    during_word = chord_during_word;
    press(1);
    assert(spoken_count == 1 && !audio_busy);
    tick(1); assert(edit_active && edit_entries == 2);
    release(1); release(13); release(16);
    edit_active = false; reload_ready = true; tick(1);
    /* Reverse press order also suppresses both mode toggles. */
    press(16); press(13); tick(1);
    assert(edit_active && edit_entries == 3);
}

static void test_exclusive_modes(void) {
    boot();
    const uint8_t colors[4][3] = {{0,0,255}, {0,255,0}, {255,0,0}, {255,255,255}};
    for (unsigned from = 0; from < 4; ++from) {
        for (unsigned to = 0; to < 4; ++to) {
            tap((unsigned[]){14,16,15,13}[from]);
            tap((unsigned[]){14,16,15,13}[to]);
            for (unsigned i = 0; i < 4; ++i) {
                bool active = from != to && i == to;
                pair(i * 2, active ? colors[i][0] : 0,
                     active ? colors[i][1] : 0, active ? colors[i][2] : 0);
            }
            if (from != to) tap((unsigned[]){14,16,15,13}[to]); /* Return to Normal. */
        }
    }
}

static void test_idle_display(void) {
    boot(); tick(3100);
    assert(!strcmp(screen[1],"SPEED 072") && !strcmp(screen[2],"PITCH 096"));
    raw1 += 799; raw2 -= 799; tick(60);
    assert(rendered_voice.speed == 72 && rendered_voice.pitch == 96);
    raw1 = 65535; raw2 = 0; tick(60);
    assert(!speech_calls && rendered_voice.speed == 1 && rendered_voice.pitch == 10);
    assert(!strcmp(screen[1],"SPEED 001") && !strcmp(screen[2],"PITCH 010"));
    tap(14); tick(60);
    assert(!strcmp(screen[0],"SAMPLE") && !strcmp(screen[1],"22050 HZ"));
    raw1=0; raw2=65535; tick(60);
    assert(!strcmp(screen[1],"88200 HZ") && !strcmp(screen[2],"8 BIT"));
    raw1=65535; raw2=0; tick(60);
    assert(!strcmp(screen[1],"08000 HZ") && !strcmp(screen[2],"1 BIT"));
    tap(15); tick(60);
    assert(!strcmp(screen[0],"CHROMATIC") && !strcmp(screen[2],"PITCH 010"));
    raw2=65535; tick(60);
    assert(!strcmp(screen[2],"PITCH 255") && !speech_calls);
    tap(13); tick(60); raw1=0; raw2=0; tick(60);
    assert(!strcmp(screen[1],"MOUTH 255") && !strcmp(screen[2],"THROAT 255"));
}

static void test_display_notices(void) {
    boot(); tick(60);
    assert(!strcmp(screen[0],"BANK 1") && !strcmp(screen[1],"LOADED"));
    tick(1500); assert(!strcmp(screen[0],"NORMAL"));
    select_bank(2); tick(60);
    assert(!strcmp(screen[0],"BANK 2") && !strcmp(screen[1],"LOADED"));
    tick(1500); assert(!strcmp(screen[0],"NORMAL"));
    lock_short(15,1); tick(60);
    assert(!strcmp(screen[0],"PARAMETER") && !strcmp(screen[1],"LOCKED"));
    tap(16); press(1); tick(2000); release(1); tick(60);
    assert(!strcmp(screen[0],"PRESET") && !strcmp(screen[1],"STORED"));
    tap(16); tap(1); tick(60);
    assert(!strcmp(screen[0],"PRESET") && !strcmp(screen[1],"LOADED"));
    tick(1500); assert(!strcmp(screen[0],"NORMAL"));
    press(13); press(16); tick(60); tick(60);
    assert(!strcmp(screen[0],"USB EDIT") && !strcmp(screen[1],"SAVE FILES") && !strcmp(screen[2],"EJECT FILES"));
    tick(4000); assert(!strcmp(screen[0],"USB EDIT"));
}

static void boot_during_word(unsigned word) {
    if (!word) {
        during_word = NULL;
        matrix_event.down = matrix_event.pressed = (1u << 13) | (1u << 14);
    }
}
static void test_bootsel(void) {
    boot();
    during_word = boot_during_word;
    press(1);
    assert(spoken_count == 1 && !audio_busy);
    tick(1);
    assert(!pvs_bootsel_ready() && !edit_active);
    assert(!strcmp(screen[0],"FIRMWARE") && !strcmp(screen[1],"FLASH"));
    assert(!strcmp(screen[2],"BUILD 042"));
    tick(1499); assert(!pvs_bootsel_ready());
    tick(1); assert(pvs_bootsel_ready());
}
static void test_bootsel_eject(void) {
    boot();
    edit_active = true;
    pvs_request_bootsel();
    tick(1);
    assert(!pvs_bootsel_ready());
    edit_active = false;
    tick(1);
    assert(!pvs_bootsel_ready());
    tick(1500); assert(pvs_bootsel_ready());
}

static void test_companion_controls(void) {
    boot();
    pvs_controls_t state;
    pvs_controls_read(&state);
    assert(state.editable && !state.hardware_mask && !state.lock_mask && state.mode == 0);
    assert(!strcmp(pvs_controls_set("speed", 88), "ok"));
    assert(!strcmp(pvs_controls_set("pitch", 101), "ok"));
    assert(!strcmp(pvs_controls_set("mouth", 150), "ok"));
    assert(!strcmp(pvs_controls_set("throat", 151), "ok"));
    assert(!strcmp(pvs_controls_set("rate", 16000), "ok"));
    assert(!strcmp(pvs_controls_set("bits", 4), "ok"));
    tick(100);
    pvs_controls_read(&state);
    assert(state.voice.speed == 88 && state.voice.pitch == 101);
    assert(state.voice.mouth == 150 && state.voice.throat == 151);
    assert(state.voice.sample_rate == 16000 && state.voice.bit_depth == 4);
    assert(rendered_voice.speed == 88 && rendered_voice.sample_rate == 16000);
    assert(!strcmp(pvs_controls_set("speed", 0), "range"));
    assert(!strcmp(pvs_controls_set("mouth", 256), "range"));
    assert(!strcmp(pvs_controls_set("rate", 7999), "range"));
    assert(!strcmp(pvs_controls_set("rate", 88201), "range"));
    assert(!strcmp(pvs_controls_set("bits", 9), "range"));
    assert(!strcmp(pvs_controls_set("nope", 1), "parameter"));
    /* Movement wins even if no display/control tick has processed it yet. */
    raw1 += 800;
    assert(!strcmp(pvs_controls_set("speed", 99), "hardware"));
    tick(60); pvs_controls_read(&state);
    assert(state.hardware_mask == 1 && state.voice.speed != 99);
    assert(!strcmp(pvs_controls_set("pitch", 111), "ok"));
    tap(13); /* Voice frees speed/pitch and rearms mouth/throat. */
    assert(!strcmp(pvs_controls_set("speed", 99), "ok"));
    assert(!strcmp(pvs_controls_set("mouth", 160), "ok"));
    raw1 += 799; tick(60);
    pvs_controls_read(&state); assert(state.voice.mouth == 160);
    ++raw1;
    assert(!strcmp(pvs_controls_set("mouth", 161), "hardware"));
    tap(15); raw2 += 800;
    assert(!strcmp(pvs_controls_set("pitch", 112), "hardware"));
    assert(!preset_writes && !lock_writes);
    edit_active = true;
    assert(!strcmp(pvs_controls_set("bits", 5), "unavailable"));
    edit_active = false; pvs_request_bootsel();
    assert(!strcmp(pvs_controls_set("bits", 5), "unavailable"));
}

static void companion_during_word(unsigned word) {
    if (word) return;
    pvs_controls_t state; pvs_controls_read(&state);
    assert(state.busy && state.lock_mask == 12);
    assert(!strcmp(pvs_controls_set("mouth", 200), "locked"));
    assert(!strcmp(pvs_controls_set("speed", 91), "ok"));
    assert(!strcmp(pvs_controls_set("rate", 16000), "ok"));
    assert(rendered_voice.speed == 72); /* Preserve the in-flight word. */
}
static void test_companion_word(void) {
    disk_locks[0][0].flags = LOCK_VOICE;
    disk_locks[0][0].voice = voice_values(72, 96, 180, 181, 22050, 8);
    boot(); during_word = companion_during_word;
    tap(1);
    assert(spoken_count == 2);
    assert(spoken[0].voice.speed == 72 && spoken[0].voice.mouth == 180);
    assert(spoken[0].rate_after_service == 16000);
    assert(spoken[1].voice.speed == 91 && spoken[1].voice.sample_rate == 16000);
    pvs_controls_t state; pvs_controls_read(&state);
    assert(!state.lock_mask && state.voice.mouth == 128);
}

static void test_midi_controls(void) {
    boot();
    pvs_controls_t state;
    for (unsigned value=0;value<128;++value) {
        for(unsigned cc=20;cc<=25;++cc) pvs_midi_cc((uint8_t)cc,(uint8_t)value);
        pvs_controls_read(&state);
        assert(state.voice.speed==1+(value*254+63)/127);
        assert(state.voice.pitch==(value*255+63)/127);
        assert(state.voice.mouth==(value*255+63)/127);
        assert(state.voice.throat==(value*255+63)/127);
        assert(state.voice.sample_rate==8000+(value*80200+63)/127);
        assert(state.voice.bit_depth==1+(value*7+63)/127);
    }
    raw1=65535; tick(60); pvs_controls_read(&state); assert(state.hardware_mask&1);
    pvs_midi_cc(20,127); tick(60); pvs_controls_read(&state);
    assert(state.voice.speed==255 && !(state.hardware_mask&1));
    raw1-=799; tick(60); pvs_controls_read(&state); assert(state.voice.speed==255);
    --raw1; tick(60); pvs_controls_read(&state); assert(state.voice.speed!=255);
    pvs_midi_cc(19,0); pvs_midi_cc(26,0); pvs_midi_cc(21,255);
    pvs_controls_read(&state); assert(state.voice.pitch==255);
    edit_active=true; pvs_midi_cc(22,0); pvs_midi_note(36,127); tick(1);
    pvs_controls_read(&state); assert(state.voice.mouth==255 && !speech_calls);
    assert(!preset_writes&&!lock_writes);
}
static void test_midi_notes(void) {
    boot();
    pvs_midi_note(35,127);pvs_midi_note(48,127);pvs_midi_note(36,0);tick(1);
    assert(!speech_calls);
    for(unsigned note=36;note<=47;++note){
        clear_spoken();pvs_midi_note((uint8_t)note,100);tick(1);
        assert(spoken_count==2);
        if(note==36)assert(!strcmp(spoken[0].text,"hello"));
        else if(note==37)assert(spoken[0].phonetic);
        else{char expected[16];snprintf(expected,sizeof expected,"phrase%u",note-35);assert(!strcmp(spoken[1].text,expected));}
    }
    tap(15);clear_spoken();pvs_midi_note(36,127);tick(1);
    assert(!strcmp(spoken[0].text,"hello")); /* Notes always address bank keys. */
    tap(16);clear_spoken();pvs_midi_note(47,127);tick(1);
    assert(!strcmp(spoken[1].text,"phrase12") && !preset_writes);
    clear_spoken();pvs_midi_note(36,127);pvs_midi_note(38,127);tick(1);
    assert(spoken_count==2&&!strcmp(spoken[1].text,"phrase3")); /* Latest note wins. */
}
static void midi_interrupt_word(unsigned word) {
    if(word)return;
    during_word=NULL;
    pvs_midi_cc(22,127);pvs_midi_note(37,100);
}
static void test_midi_retrigger(void) {
    boot();during_word=midi_interrupt_word;
    pvs_midi_note(36,127);tick(1);
    assert(spoken_count==3 && !strcmp(spoken[0].text,"hello"));
    assert(spoken[1].phonetic && spoken[1].voice.mouth==255);
}
static void midi_locked_word(unsigned word) {
    (void)word;pvs_midi_cc(22,127);pvs_midi_cc(24,127);
}
static void test_midi_locks(void) {
    disk_locks[0][0].flags=LOCK_VOICE|LOCK_SAMPLE;
    disk_locks[0][0].voice=voice_values(72,96,180,181,12350,3);
    boot();during_word=midi_locked_word;pvs_midi_note(36,127);tick(1);
    assert(spoken_count==2);
    for(unsigned i=0;i<2;++i){assert(spoken[i].voice.mouth==180);assert(spoken[i].rate_after_service==12350);}
    pvs_controls_t state;pvs_controls_read(&state);
    assert(state.voice.mouth==255&&state.voice.sample_rate==88200);
    assert(rendered_voice.mouth==255&&rendered_voice.sample_rate==88200);
}

static void metadata_during_word(unsigned word) {
    (void)word;pvs_bank_state_t state;pvs_bank_read(&state);
    assert(state.playing==0);
}
static void test_phrase_metadata(void) {
    boot();pvs_bank_state_t before,after;pvs_bank_read(&before);
    assert(before.bank==1&&before.revision>0&&before.playing==-1);
    pvs_phrase_chunk_t phrase;
    for(unsigned key=0;key<12;++key){
        assert(pvs_phrase_chunk(key,0,&phrase));
        assert(phrase.bank==1&&phrase.revision==before.revision&&phrase.length>0);
        assert(phrase.kind==(key==1?'P':'S')&&!phrase.locks);
    }
    lock_short(13,3);pvs_bank_read(&after);assert(after.revision>before.revision);
    assert(pvs_phrase_chunk(2,0,&phrase)&&phrase.locks==LOCK_VOICE);
    before=after;press(13);press(3);tick(2000);release(3);release(13);
    pvs_bank_read(&after);assert(after.revision>before.revision);
    assert(pvs_phrase_chunk(2,0,&phrase)&&!phrase.locks);
    during_word=metadata_during_word;
    assert(!strcmp(pvs_trigger_key(0),"ok"));tick(1);during_word=NULL;
    pvs_bank_read(&after);assert(after.playing==-1);
    select_bank(2);pvs_bank_read(&after);assert(after.bank==2);
    assert(pvs_phrase_chunk(0,0,&phrase));assert(!memcmp(phrase.text,"bank2 phrase1",13));
    edit_active=true;assert(!strcmp(pvs_trigger_key(0),"unavailable"));
}
static void test_phrase_long(void) {
    for(unsigned i=0;i<sizeof bank_one_text-1;++i)bank_one_text[i]=(char)('A'+i%26);
    bank_one_text[sizeof bank_one_text-1]=0;
    boot();char collected[PVS_MAX_PHRASE]={0};pvs_phrase_chunk_t phrase;
    for(unsigned chunk=0;chunk<8;++chunk){
        assert(pvs_phrase_chunk(0,chunk,&phrase));
        assert(phrase.length==383&&phrase.count==(chunk==7?47:48));
        memcpy(collected+chunk*48,phrase.text,phrase.count);
    }
    assert(!strcmp(collected,bank_one_text));
    assert(!pvs_phrase_chunk(12,0,&phrase)&&!pvs_phrase_chunk(0,8,&phrase));
    assert(pvs_phrase_chunk(1,7,&phrase)&&phrase.count==0);
}
static void test_phrase_empty(void) {
    mock_phrase_count=3;boot();pvs_phrase_chunk_t phrase;
    assert(pvs_phrase_chunk(11,0,&phrase)&&phrase.kind=='E'&&!phrase.length&&!phrase.count&&!phrase.locks);
    assert(!strcmp(pvs_trigger_key(11),"range"));
}

int main(int argc, char **argv) {
    assert(argc == 2);
    if (!strcmp(argv[1], "bootsel")) { test_bootsel(); return 0; }
    if (!strcmp(argv[1], "bootsel_eject")) { test_bootsel_eject(); return 0; }
    if (!strcmp(argv[1], "phrase_metadata")) test_phrase_metadata();
    else if (!strcmp(argv[1], "phrase_long")) test_phrase_long();
    else if (!strcmp(argv[1], "phrase_empty")) test_phrase_empty();
    else if (!strcmp(argv[1], "midi_controls")) test_midi_controls();
    else if (!strcmp(argv[1], "midi_notes")) test_midi_notes();
    else if (!strcmp(argv[1], "midi_retrigger")) test_midi_retrigger();
    else if (!strcmp(argv[1], "midi_locks")) test_midi_locks();
    else if (!strcmp(argv[1], "companion_controls")) test_companion_controls();
    else if (!strcmp(argv[1], "companion_word")) test_companion_word();
    else if (!strcmp(argv[1], "startup")) test_startup();
    else if (!strcmp(argv[1], "modes_pickup")) test_modes_pickup();
    else if (!strcmp(argv[1], "pickup_threshold")) test_pickup_threshold();
    else if (!strcmp(argv[1], "live_modulation")) test_live_modulation();
    else if (!strcmp(argv[1], "word_modulation")) test_word_modulation();
    else if (!strcmp(argv[1], "chromatic_last")) test_chromatic_last();
    else if (!strcmp(argv[1], "presets")) test_presets();
    else if (!strcmp(argv[1], "locks")) test_locks();
    else if (!strcmp(argv[1], "locked_sample")) test_locked_sample();
    else if (!strcmp(argv[1], "edit")) test_edit();
    else if (!strcmp(argv[1], "format")) test_format();
    else if (!strcmp(argv[1], "bank_priority")) test_bank_priority();
    else if (!strcmp(argv[1], "retrigger")) test_retrigger();
    else if (!strcmp(argv[1], "edit_chord")) test_edit_chord();
    else if (!strcmp(argv[1], "exclusive_modes")) test_exclusive_modes();
    else if (!strcmp(argv[1], "idle_display")) test_idle_display();
    else if (!strcmp(argv[1], "display_notices")) test_display_notices();
    else assert(!"unknown scenario");
    printf("PASS %s\n", argv[1]);
    return 0;
}

void pvs_memory_init(void) {}
void pvs_memory_print_status(void) {}

void pvs_build_print_status(void) {}

void oled_init(void) {}
void oled_show(const char *mode, const char *p1, const char *p2) {
    snprintf(screen[0],32,"%s",mode);
    snprintf(screen[1],32,"%s",p1);
    snprintf(screen[2],32,"%s",p2);
}
void oled_task(void) {}
void oled_print_status(void) {}

void oled_message(const char *top, const char *middle, const char *bottom) {
    snprintf(screen[0],32,"%s",top);
    snprintf(screen[1],32,"%s",middle);
    snprintf(screen[2],32,"%s",bottom);
}

void oled_notice(const char *top, const char *middle) { oled_message(top,middle,""); }

bool storage_prepare_bootsel(void) { assert(!audio_busy && !edit_active); return true; }

unsigned pvs_build_number(void) { return 42; }
void oled_flash_screen(unsigned build) {
    char label[24]; snprintf(label,sizeof label,"BUILD %03u",build);
    oled_message("FIRMWARE","FLASH",label);
}

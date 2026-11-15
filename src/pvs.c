#include "oled.h"
#include "keypad.h"
#include "pvs_memory.h"
#include "build_info.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pvs.h"
#include "pvs_config.h"
#include "matrix.h"
#include "pots.h"
#include "storage.h"
#include "sam_pvs.h"
#include "ws2812.h"

typedef enum {
    POT_SPEED, POT_PITCH, POT_MOUTH, POT_THROAT, POT_RATE, POT_BITS,
    POT_CHROMATIC, POT_UNASSIGNED
} pot_context_t;
typedef struct {
    pot_context_t context;
    uint16_t reference;
    bool active;
} pickup_t;

/* User-selected modes are mutually exclusive; a bare active-mode tap
 * returns to Normal. Modifier chords retain their separate semantics. */
static bool sample_mode, pitch_mode, voice_mode, preset_mode;
static bool modifier_down[4], modifier_used[4];
static phrase_bank_t phrases, bank_scratch;
static unsigned bank = 1, last_preset;
static sam_voice_t voice = {
    PVS_DEFAULT_SPEED, PVS_DEFAULT_PITCH, PVS_DEFAULT_MOUTH,
    PVS_DEFAULT_THROAT, PVS_DEFAULT_RATE, 8
};
static sam_voice_t presets[12];
static uint16_t preset_valid;
static phrase_lock_t locks[12];
static pickup_t pickup[2] = {{POT_UNASSIGNED, 0, false}, {POT_UNASSIGNED, 0, false}};
static phrase_t last_phrase;
static phrase_lock_t last_lock, active_lock;
static bool have_last_phrase, phrase_active;
static int last_button = -1;
static float active_ratio = 1.0f;
static int lock_button = -1, preset_button = -1;
static uint8_t lock_group;
static uint32_t lock_started, preset_started;
static bool lock_cleared;
static bool initialized;
static int playing_button = -1;
static int requested_button = -1;
static bool button_edit;
#define DEFERRED_EVENTS 64u
static matrix_state_t deferred[DEFERRED_EVENTS];
static unsigned deferred_read, deferred_count;

static bool boot_request, boot_ready, boot_prepared;
static uint32_t boot_screen_started;
void pvs_request_bootsel(void) {
    if (!boot_request)
        printf("BOOTSEL requested; eject the PVS editing disk first if mounted.\n");
    boot_request = true;
    if (sam_pvs_is_busy()) sam_pvs_stop();
}
bool pvs_bootsel_ready(void) { return boot_ready; }

static bool edit_chord(matrix_state_t state) {
    const uint16_t boot_chord = (1u << 13) | (1u << 14);
    if ((state.down & boot_chord) == boot_chord && (state.pressed & boot_chord)) {
        modifier_used[1] = modifier_used[2] = true;
        pvs_request_bootsel();
        return true;
    }
    const uint16_t chord = (1u << 12) | (1u << 15);
    if ((state.down & chord) != chord || !(state.pressed & chord)) return false;
    button_edit = true;
    modifier_used[0] = modifier_used[3] = true;
    if (sam_pvs_is_busy()) sam_pvs_stop();
    printf("A+D: USB edit requested\n");
    return true;
}

static const float pitch_ratios[12] = {
    1.000000f, 0.943874f, 0.890899f, 0.840896f, 0.793701f, 0.749154f,
    0.707107f, 0.667420f, 0.629961f, 0.594604f, 0.561231f, 0.529732f
};
static const char *const pitch_names[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};
static const char *const context_names[] = {
    "speed", "pitch", "mouth", "throat", "sample rate", "bit depth", "chromatic pitch", "unassigned"
};

static int map_raw(uint16_t raw, int minimum, int maximum) {
    /* Compute before division so descending ranges match Python int(float),
     * which truncates the whole positive mapped value, not its negative delta. */
    return (int)(((int64_t)minimum * 65535 + (int64_t)raw * (maximum - minimum)) / 65535);
}

static uint32_t rate_from_raw(uint16_t raw) {
    raw = (uint16_t)(65535u - raw);
    uint32_t rate;
    if (raw <= 32767u)
        rate = PVS_MIN_RATE + (uint32_t)raw * (PVS_DEFAULT_RATE - PVS_MIN_RATE) / 32767u;
    else
        rate = PVS_DEFAULT_RATE + (uint32_t)(raw - 32768u) * (PVS_MAX_RATE - PVS_DEFAULT_RATE) / 32767u;
    return rate / 50u * 50u;
}

static uint8_t bits_from_raw(uint16_t raw) {
    static const uint16_t boundaries[] = {13107, 26214, 36044, 45875, 52428, 57671, 61603};
    for (unsigned i = 0; i < 7; ++i)
        if (raw < boundaries[i]) return (uint8_t)(i + 1u);
    return 8;
}

#define DISPLAY_NOTICE_US 3000000u
static char notice_top[24], notice_middle[24];
static uint32_t notice_started, notice_duration;
static bool notice_active;
static void display_notice(const char *top, const char *middle) {
    snprintf(notice_top,sizeof notice_top,"%s",top);
    snprintf(notice_middle,sizeof notice_middle,"%s",middle);
    notice_started = time_us_32();
    notice_duration = !strcmp(middle,"LOADED") ? DISPLAY_NOTICE_US / 2u : DISPLAY_NOTICE_US;
    notice_active = true;
    oled_notice(notice_top,notice_middle);
}

static void update_pots(bool announce);

static void display_task(void) {
    static uint32_t last_update;
    uint32_t now = time_us_32();
    if ((uint32_t)(now - last_update) >= 50000u) {
        last_update = now;
        if (!sam_pvs_is_busy() && !storage_edit_active()) update_pots(false);
        char first[24], second[24];
        /* Display the controls the pots currently edit, rather than a
         * phrase lock's temporary override. */
        const char *mode = storage_edit_active() ? "USB EDIT" : preset_mode ? "PRESET" :
            sample_mode ? "SAMPLE" : pitch_mode ? "CHROMATIC" : voice_mode ? "VOICE" : "NORMAL";
        if (storage_edit_active()) {
            notice_active = false;
            oled_message("USB EDIT","SAVE FILES","EJECT FILES");
        } else if (notice_active && (uint32_t)(now-notice_started) < notice_duration) {
            oled_notice(notice_top,notice_middle);
        } else {
            notice_active = false;
            if (sample_mode) {
                snprintf(first,sizeof first,"%05lu HZ",(unsigned long)voice.sample_rate);
                snprintf(second,sizeof second,"%u BIT",voice.bit_depth);
            } else if (voice_mode) {
                snprintf(first,sizeof first,"MOUTH %03u",voice.mouth);
                snprintf(second,sizeof second,"THROAT %03u",voice.throat);
            } else {
                snprintf(first,sizeof first,"SPEED %03u",voice.speed);
                snprintf(second,sizeof second,"PITCH %03u",voice.pitch);
            }
            oled_show(mode,first,second);
        }
    }
    oled_task();
}

static void print_voice(void) {
    printf("Speed:%u Pitch:%u Mouth:%u Throat:%u Rate:%luHz Bits:%u\n",
           voice.speed, voice.pitch, voice.mouth, voice.throat,
           (unsigned long)voice.sample_rate, voice.bit_depth);
}

static void update_leds(void) {
    ws2812_clear();
    for (unsigned i = 0; i < 2; ++i) {
        if (sample_mode) ws2812_set(i, 0, 0, 255);
        if (preset_mode) ws2812_set(i + 2, 0, 255, 0);
        if (pitch_mode) ws2812_set(i + 4, 255, 0, 0);
        if (voice_mode) ws2812_set(i + 6, 255, 255, 255);
    }
    ws2812_show();
}

static void update_contexts(bool force) {
    pot_context_t contexts[2] = {
        sample_mode ? POT_RATE : voice_mode ? POT_MOUTH : POT_SPEED,
        pitch_mode ? POT_CHROMATIC : sample_mode ? POT_BITS : voice_mode ? POT_THROAT : POT_PITCH
    };
    uint16_t raw[2];
    pots_read(&raw[0], &raw[1]);
    for (unsigned i = 0; i < 2; ++i) {
        if (force || contexts[i] != pickup[i].context) {
            pickup[i] = (pickup_t){contexts[i], raw[i], false};
            printf("Pot%u -> %s (waiting for movement)\n", i + 1, context_names[contexts[i]]);
        }
    }
}

static bool pot_moved(unsigned pot, uint16_t raw) {
    if (!pickup[pot].active && abs((int)raw - (int)pickup[pot].reference) >= PVS_PICKUP_THRESHOLD) {
        pickup[pot].active = true;
        printf("Pot%u active\n", pot + 1);
    }
    return pickup[pot].active;
}

static bool context_locked(pot_context_t context, uint8_t flags) {
    switch (context) {
        case POT_SPEED: case POT_PITCH: case POT_CHROMATIC: return (flags & LOCK_PITCH) != 0;
        case POT_MOUTH: case POT_THROAT: return (flags & LOCK_VOICE) != 0;
        case POT_RATE: case POT_BITS: return (flags & LOCK_SAMPLE) != 0;
        default: return false;
    }
}

static void effective_voice(void) {
    sam_voice_t effective = voice;
    if (phrase_active) {
        if (active_lock.flags & LOCK_PITCH) {
            effective.speed = active_lock.voice.speed;
            effective.pitch = active_lock.voice.pitch;
        }
        if (active_lock.flags & LOCK_VOICE) {
            effective.mouth = active_lock.voice.mouth;
            effective.throat = active_lock.voice.throat;
        }
        if (active_lock.flags & LOCK_SAMPLE) {
            effective.sample_rate = active_lock.voice.sample_rate;
            effective.bit_depth = active_lock.voice.bit_depth;
        }
    }
    unsigned pitch = (unsigned)((float)effective.pitch * active_ratio + 0.5f);
    if (pitch < 1u) pitch = 1u;
    if (pitch > 255u) pitch = 255u;
    effective.pitch = (uint8_t)pitch;
    sam_pvs_set_voice(&effective);
}

static void update_pots(bool announce) {
    uint16_t raw[2];
    sam_voice_t old = voice;
    uint8_t flags = phrase_active ? active_lock.flags : 0;
    pots_read(&raw[0], &raw[1]);
    for (unsigned i = 0; i < 2; ++i) {
        pot_context_t context = pickup[i].context;
        if (context_locked(context, flags) || !pot_moved(i, raw[i])) continue;
        switch (context) {
            case POT_SPEED: voice.speed = (uint8_t)map_raw(raw[i], 255, 1); break;
            case POT_PITCH: case POT_CHROMATIC: voice.pitch = (uint8_t)map_raw(raw[i], 10, 255); break;
            case POT_MOUTH: voice.mouth = (uint8_t)map_raw(raw[i], 255, 1); break;
            case POT_THROAT: voice.throat = (uint8_t)map_raw(raw[i], 255, 1); break;
            case POT_RATE: voice.sample_rate = rate_from_raw(raw[i]); break;
            case POT_BITS: voice.bit_depth = bits_from_raw(raw[i]); break;
            default: break;
        }
    }
    effective_voice();
    if (announce && (old.speed != voice.speed || old.pitch != voice.pitch || old.mouth != voice.mouth ||
        old.throat != voice.throat || old.sample_rate != voice.sample_rate || old.bit_depth != voice.bit_depth))
        print_voice();
}

static void before_word(void *unused) {
    (void)unused;
    update_pots(true);
}

static void speech_service(void *unused) {
    (void)unused;
    /* Phrase-key and edit requests interrupt playback. Defer all other
     * events, and perform filesystem ownership changes after audio stops. */
    matrix_task();
    if (deferred_count < DEFERRED_EVENTS) {
        matrix_state_t event = matrix_scan();
        if (!edit_chord(event)) {
            if (playing_button >= 0 && !(event.down & 0xf000u)) {
                for (unsigned button = 0; button < 12; ++button) {
                    if (!(event.pressed & (1u << button))) continue;
                    if (!pitch_mode && button >= phrases.count) continue;
                    requested_button = (int)button;
                    event.pressed &= (uint16_t)~(1u << button);
                    sam_pvs_stop();
                    break;
                }
            }
            if (event.pressed || event.released) {
                deferred[(deferred_read + deferred_count) % DEFERRED_EVENTS] = event;
                ++deferred_count;
            }
        }
    }
    ws2812_task();
    storage_console_service();
    uint32_t target = voice.sample_rate;
    if (phrase_active && (active_lock.flags & LOCK_SAMPLE)) {
        target = active_lock.voice.sample_rate;
    } else if (sample_mode) {
        uint16_t raw1, raw2;
        pots_read(&raw1, &raw2);
        if (pot_moved(0, raw1)) {
            voice.sample_rate = rate_from_raw(raw1);
            target = voice.sample_rate;
        }
    }
    sam_pvs_set_live_rate(target);
    display_task();
}

static void reset_lock_combo(void) {
    lock_button = -1;
    lock_group = 0;
    lock_cleared = false;
}

static void save_startup(void) {
    if (!storage_save_startup(bank, last_preset)) printf("Unable to save startup state\n");
}

static bool load_bank(unsigned selected, bool save) {
    if (!storage_load_bank(selected, &bank_scratch)) {
        printf("Phrase bank %u load failed\n", selected);
        return false;
    }
    phrases = bank_scratch;
    bank = selected;
    memset(locks, 0, sizeof locks);
    if (!storage_load_locks(bank, locks)) printf("Bank %u lock file unavailable\n", bank);
    if (save) save_startup();
    printf("Phrase bank %u loaded (%u phrases)\n", bank, phrases.count);
    char title[24];
    snprintf(title,sizeof title,"BANK %u",bank);
    display_notice(title,"LOADED");
    return true;
}

static bool recall_preset(unsigned slot, bool announce, bool save) {
    if (slot < 1 || slot > 12 || !(preset_valid & (1u << (slot - 1u)))) {
        if (announce) printf("PRESET %c IS EMPTY\n", keypad_label(slot - 1));
        return false;
    }
    voice = presets[slot - 1u];
    sam_pvs_set_voice(&voice);
    last_preset = slot;
    if (save) save_startup();
    if (announce) {
        update_contexts(true);
        printf("PRESET %c LOADED\n", keypad_label(slot - 1));
        display_notice("PRESET","LOADED");
        print_voice();
        sam_pvs_say("Preset loaded");
    }
    return true;
}

static void store_preset(unsigned slot) {
    presets[slot - 1u] = voice;
    preset_valid |= (uint16_t)(1u << (slot - 1u));
    if (!storage_save_presets(presets, preset_valid)) {
        printf("Unable to save preset %c\n", keypad_label(slot - 1));
        return;
    }
    printf("PRESET %c STORED\n", keypad_label(slot - 1));
    display_notice("PRESET","STORED");
    print_voice();
    sam_pvs_say("Preset stored");
}

static const char *group_name(uint8_t group) {
    return group == LOCK_PITCH ? "PITCH" : group == LOCK_VOICE ? "VOICE" : "SAMPLE";
}

static void store_lock(void) {
    before_word(NULL);
    phrase_lock_t *lock = &locks[lock_button];
    if (!lock->flags) lock->voice = voice;
    if (lock_group == LOCK_PITCH) {
        lock->voice.speed = voice.speed;
        lock->voice.pitch = voice.pitch;
    } else if (lock_group == LOCK_VOICE) {
        lock->voice.mouth = voice.mouth;
        lock->voice.throat = voice.throat;
    } else {
        lock->voice.sample_rate = voice.sample_rate;
        lock->voice.bit_depth = voice.bit_depth;
    }
    lock->flags |= lock_group;
    if (!storage_save_locks(bank, locks)) printf("Unable to save phrase locks\n");
    else display_notice("PARAMETER","LOCKED");
    printf("%s LOCK STORED: bank%u key %c flags%u\n", group_name(lock_group), bank, keypad_label((unsigned)lock_button), lock->flags);
}

static void clear_lock(void) {
    locks[lock_button].flags &= (uint8_t)~lock_group;
    if (!storage_save_locks(bank, locks)) printf("Unable to save phrase locks\n");
    printf("%s LOCK CLEARED: bank%u key %c\n", group_name(lock_group), bank, keypad_label((unsigned)lock_button));
    lock_cleared = true;
}

static void speak_phrase(unsigned button) {
    /* Restart only after the renderer unwinds; never recurse into SAM. */
    for (;;) {
        requested_button = -1;
        if (pitch_mode) {
            if (!have_last_phrase) {
                printf("No previous phrase\n");
                return;
            }
            active_ratio = pitch_ratios[button];
            printf("Pitch mode %s: previous phrase %c\n", pitch_names[button], keypad_label((unsigned)last_button));
        } else {
            before_word(NULL);
            if (button >= phrases.count) {
                printf("No phrase assigned to key %c\n", keypad_label(button));
                return;
            }
            last_phrase = phrases.item[button];
            last_lock = locks[button];
            last_button = (int)button;
            have_last_phrase = true;
            active_ratio = 1.0f;
        }
        active_lock = last_lock;
        phrase_active = true;
        printf("%c: %s (locks:%u)\n", keypad_label((unsigned)last_button), last_phrase.text, active_lock.flags);
        bool ok;
        playing_button = (int)button;
        ok = last_phrase.kind == PHRASE_PHONETIC ? sam_pvs_say_phonetic(last_phrase.text) : sam_pvs_say(last_phrase.text);
        playing_button = -1;
        phrase_active = false;
        active_ratio = 1.0f;
        sam_pvs_set_voice(&voice);
        if (requested_button >= 0 && !button_edit && !boot_request) {
            button = (unsigned)requested_button;
            continue;
        }
        if (!ok) printf("Speech rejected or interrupted\n");
        break;
    }
}

void pvs_reload_storage(void) {
    if (sam_pvs_is_busy() || storage_edit_active()) return;
    preset_valid = 0;
    memset(presets, 0, sizeof presets);
    storage_load_presets(presets, &preset_valid);
    unsigned selected = 1, preset = 0;
    storage_load_startup(&selected, &preset);
    if (!load_bank(selected, false) && !load_bank(1, false)) {
        memset(&phrases, 0, sizeof phrases);
        memset(locks, 0, sizeof locks);
        bank = 1;
        printf("No readable phrase bank\n");
    }
    last_preset = 0;
    if (preset) recall_preset(preset, false, false);
    update_contexts(true);
    sam_pvs_set_voice(&voice);
}

void pvs_print_status(void) {
    if (!initialized) return;
    pvs_build_print_status();
    oled_print_status();
    printf("PVS native SAM: bank%u startup preset%c phrases%u storage:%s\n",
           bank, last_preset ? keypad_label(last_preset - 1) : '-', phrases.count,
           storage_edit_active() ? "USB EDIT" : storage_is_ready() ? "mounted" : "factory fallback");
    printf("Keypad: A=Voice B=Sample C=Chromatic D=Preset/bank; A+D=USB edit; B+C=BOOTSEL\n");
    print_voice();
    printf("Modes sample:%u preset:%u pitch:%u voice:%u; word modulation ON\n",
           sample_mode, preset_mode, pitch_mode, voice_mode);
    printf("Pot1:%s %s Pot2:%s %s\n", context_names[pickup[0].context], pickup[0].active ? "active" : "waiting",
           context_names[pickup[1].context], pickup[1].active ? "active" : "waiting");
    matrix_print_status();
    pots_print_status();
    sam_pvs_print_status();
    pvs_memory_print_status();
    printf("WS2812 GP22: PIO1 SM1, eight LEDs; USB serial: ? for commands\n");
}

void pvs_init(void) {
    sam_pvs_init();
    matrix_init();
    pots_init();
    ws2812_init();
    oled_init();
    storage_init();
    storage_set_status_callback(pvs_print_status);
    sam_pvs_set_service_callback(speech_service, NULL);
    sam_pvs_set_word_callback(before_word, NULL);
    pvs_memory_init();
    initialized = true;
    pvs_reload_storage();
    update_leds();
    pvs_print_status();
    printf("A voice; B sample; C chromatic; D presets / hold+phrase key selects bank\n"
           "Hold B/C/A + phrase: release stores sample/pitch/voice lock; hold2s clears\n"
           "Hold D + B clears bank locks. Presets: tap loads, hold2s stores.\n"
           "A+D: USB edit; B+C or serial bootsel: firmware update. Repress playing phrase to restart.\n");
}

void pvs_task(void) {
    if (boot_prepared) oled_task();
    else display_task();
    storage_console_service();
    ws2812_task();
    if (boot_request) {
        if (!sam_pvs_is_busy() && !storage_edit_active() && !boot_ready) {
            if (!boot_prepared && storage_prepare_bootsel()) {
                boot_prepared = true;
                boot_screen_started = time_us_32();
                oled_flash_screen(pvs_build_number());
            } else if (!boot_prepared) {
                boot_request = false;
                printf("BOOTSEL cancelled: storage could not be synchronized.\n");
            }
            if (boot_prepared && (uint32_t)(time_us_32()-boot_screen_started) >= 1500000u)
                boot_ready = true;
        }
        return;
    }
    if (!sam_pvs_is_busy() && storage_format_requested()) {
        if (!storage_format_factory()) printf("Storage factory format failed or refused\n");
    }
    bool serial_edit = !sam_pvs_is_busy() && storage_edit_requested();
    if (!sam_pvs_is_busy() && (button_edit || serial_edit)) {
        button_edit = false;
        if (storage_begin_edit()) {
            deferred_count = deferred_read = 0;
            reset_lock_combo();
            preset_button = -1;
            preset_mode = false;
            memset(modifier_down, 0, sizeof modifier_down);
            memset(modifier_used, 0, sizeof modifier_used);
            modifier_used[0] = modifier_used[3] = true;
            update_leds();
        }
    }
    if (storage_edit_active()) {
        /* Drain events during editing so edits cannot enqueue speech. */
        (void)edit_chord(matrix_scan());
        return;
    }
    if (storage_reload_pending()) pvs_reload_storage();
    matrix_state_t state;
    if (deferred_count) {
        state = deferred[deferred_read];
        deferred_read = (deferred_read + 1) % DEFERRED_EVENTS;
        --deferred_count;
    } else state = matrix_scan();
    if (edit_chord(state)) return;
    uint32_t now = state.time_ms;
    for (unsigned i = 0; i < 4; ++i) {
        if (state.pressed & (1u << (12u + i))) {
            modifier_down[i] = true;
            modifier_used[i] = false;
        }
    }
    if (modifier_down[3] && (state.pressed & (1u << 13))) {
        modifier_used[1] = modifier_used[3] = true;
        reset_lock_combo();
        memset(locks, 0, sizeof locks);
        if (!storage_save_locks(bank, locks)) printf("Unable to save cleared locks\n");
        printf("ALL PARAMETER LOCKS CLEARED: bank%u\n", bank);
    }
    if (modifier_down[3]) {
        for (unsigned button = 0; button < 12; ++button) {
            if (state.pressed & (1u << button)) {
                modifier_used[3] = true;
                load_bank(button + 1u, true);
            }
        }
    }
    if (!modifier_down[3] && lock_button < 0) {
        for (unsigned button = 0; button < 12; ++button) {
            if (!(state.pressed & (1u << button))) continue;
            int modifier = modifier_down[1] ? 1 : modifier_down[2] ? 2 : modifier_down[0] ? 0 : -1;
            if (modifier < 0) break;
            modifier_used[modifier] = true;
            lock_group = modifier == 1 ? LOCK_SAMPLE : modifier == 2 ? LOCK_PITCH : LOCK_VOICE;
            lock_button = (int)button;
            lock_started = now;
            lock_cleared = false;
            printf("%s LOCK key %c: release stores; hold2s clears\n", group_name(lock_group), keypad_label(button));
            break;
        }
    }
    if (lock_button >= 0) {
        uint16_t bit = (uint16_t)(1u << lock_button);
        uint32_t elapsed = now - lock_started;
        /* Release duration also handles a hold fully captured during speech. */
        if (!lock_cleared && elapsed >= PVS_LOCK_HOLD_MS && ((state.down | state.released) & bit)) clear_lock();
        if (state.released & bit) {
            if (!lock_cleared) store_lock();
            reset_lock_combo();
        }
    }
    /* A bare mode-button release selects one mode, or returns its active
     * mode to Normal. Used bank/lock/edit chords never select a mode. */
    bool *modes[] = {&voice_mode, &sample_mode, &pitch_mode, &preset_mode};
    const char *names[] = {"VOICE", "SAMPLE", "PITCH", "PRESET"};
    bool changed_mode = false;
    for (unsigned i = 0; i < 4; ++i) {
        if (!(state.released & (1u << (12u + i))) || modifier_used[i]) continue;
        bool enable = !*modes[i];
        sample_mode = preset_mode = pitch_mode = voice_mode = false;
        *modes[i] = enable;
        preset_button = -1;
        printf("%s MODE %s\n", names[i], enable ? "ON" : "OFF");
        changed_mode = true;
    }
    if (changed_mode) {
        update_contexts(true);
        update_leds();
    }
    /* Clear released modifier latches even in preset mode. The Python loop
     * skips these releases there and can otherwise leave a modifier stuck. */
    for (unsigned i = 0; i < 4; ++i)
        if (state.released & (1u << (12u + i))) modifier_down[i] = false;
    if (preset_mode) {
        if (preset_button < 0) {
            for (unsigned button = 0; button < 12; ++button) {
                if (state.pressed & (1u << button)) {
                    preset_button = (int)button;
                    preset_started = now;
                    break;
                }
            }
        }
        if (preset_button >= 0) {
            uint16_t bit = (uint16_t)(1u << preset_button);
            uint32_t elapsed = now - preset_started;
            if (elapsed >= PVS_PRESET_HOLD_MS && ((state.down | state.released) & bit)) {
                store_preset((unsigned)preset_button + 1u);
                preset_mode = false;
                preset_button = -1;
                update_leds();
            } else if (state.released & bit) {
                recall_preset((unsigned)preset_button + 1u, true, true);
                preset_mode = false;
                preset_button = -1;
                update_leds();
            }
        }
        return;
    }
    bool modifier = lock_button >= 0;
    for (unsigned i = 0; i < 4; ++i) modifier |= modifier_down[i];
    if (!modifier) {
        for (unsigned button = 0; button < 12; ++button)
            if (state.pressed & (1u << button)) speak_phrase(button);
    }
}

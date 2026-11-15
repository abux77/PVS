#include "sam_pvs.h"
#include "pvs_memory.h"
#include "sam_engine.h"
#include "audio_pwm.h"
#include "pvs_config.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static sam_voice_t voice = {72, 96, 128, 128, 22050, 8};
static sam_phonemes_t phonemes;
static char phonetic_text[SAM_PHONEME_CAPACITY];
static bool busy, cancelled;
#define CACHE_SEGMENTS 96u
typedef struct { size_t offset, count; uint8_t speed, pitch, mouth, throat; } cache_segment_t;
static cache_segment_t segments[CACHE_SEGMENTS];
static char cache_text[PVS_MAX_PHRASE];
static bool cache_phonetic, capturing, capture_overflow;
static size_t cache_used, segment_count, segment_cursor, capture_start;
static uint8_t *cache_pcm;
static size_t cache_capacity;
static unsigned cache_hits, cache_misses;

static void (*service_callback)(void *), (*word_callback)(void *);
static void *service_context, *word_context;

unsigned sam_pvs_cache_hits(void) { return cache_hits; }

void sam_pvs_init(void) { audio_pwm_init(voice.sample_rate); }
void sam_pvs_print_status(void) {
    size_t capacity;
    (void)pvs_sample_buffer(&capacity);
    printf("Board:%s CPU:%luMHz SAM upstream C renderer\n", PICO_BOARD,
           (unsigned long)(clock_get_hz(clk_sys) / 1000000u));
    audio_pwm_print_status();
    printf("Sample cache: %lu/%luB, %lu segments, hits%u renders%u\n",
           (unsigned long)cache_used, (unsigned long)capacity,
           (unsigned long)segment_count, cache_hits, cache_misses);
}
void sam_pvs_set_voice(const sam_voice_t *selected) {
    if (!selected) return;
    voice = *selected;
    if (!voice.speed) voice.speed = 1;
    if (voice.bit_depth < 1) voice.bit_depth = 1;
    if (voice.bit_depth > 8) voice.bit_depth = 8;
    sam_pvs_set_live_rate(voice.sample_rate);
}
void sam_pvs_set_live_rate(uint32_t rate) {
    if (rate < PVS_MIN_RATE) rate = PVS_MIN_RATE;
    if (rate > PVS_MAX_RATE) rate = PVS_MAX_RATE;
    voice.sample_rate = rate;
    audio_pwm_set_rate(rate);
}
void sam_pvs_set_service_callback(void (*callback)(void *), void *context) {
    service_callback = callback;
    service_context = context;
    audio_pwm_set_service_callback(callback, context);
}
void sam_pvs_set_word_callback(void (*callback)(void *), void *context) {
    word_callback = callback;
    word_context = context;
}
bool sam_pvs_is_busy(void) { return busy; }
void sam_pvs_stop(void) { cancelled = true; audio_pwm_stop(); }

static void service(void) {
    if (service_callback) service_callback(service_context);
    tight_loop_contents();
}
static bool pcm_sink(const uint8_t *samples, size_t count, void *unused) {
    (void)unused;
    if (capturing && !capture_overflow) {
        if (count <= cache_capacity - cache_used) {
            memcpy(cache_pcm + cache_used, samples, count);
            cache_used += count;
        } else capture_overflow = true;
    }
    return !cancelled && audio_pwm_write(samples, count);
}
static bool render_word(const char *text, bool phonetic) {
    size_t index = segment_cursor++;
    cache_segment_t key = {0, 0, voice.speed, voice.pitch, voice.mouth, voice.throat};
    if (index < segment_count) {
        cache_segment_t *entry = &segments[index];
        if (entry->speed == key.speed && entry->pitch == key.pitch &&
            entry->mouth == key.mouth && entry->throat == key.throat) {
            ++cache_hits;
            audio_pwm_begin(voice.bit_depth);
            bool okay = true;
            for (size_t n = 0; n < entry->count && okay && !cancelled;) {
                size_t count = entry->count - n;
                if (count > 128) count = 128;
                okay = audio_pwm_write(cache_pcm + entry->offset + n, count);
                n += count;
            }
            if (okay && !cancelled) okay = audio_pwm_end();
            else audio_pwm_stop();
            return okay && !cancelled;
        }
        cache_used = entry->offset;
        segment_count = index;
    }
    ++cache_misses;
    if (!phonetic) {
        if (!sam_recite(text, phonetic_text, sizeof phonetic_text)) return false;
        text = phonetic_text;
    }
    if (phonetic) {
        if (!sam_parse(text, &phonemes)) {
            /* Supplied banks include legacy HWAAN and ...NEHTIHCS... spellings.
             * Python warns and skips their unrecognized letters. Retain that
             * compatibility, while rejecting control bytes and invalid signs. */
            for (const unsigned char *c = (const unsigned char *)text; *c; ++c)
                if (!((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') ||
                      (*c >= '1' && *c <= '8') || strchr("/.,?-", *c))) return false;
            if (!sam_parse_recited(text, &phonemes) || !phonemes.count) return false;
            printf("SAM: skipped unrecognized legacy phoneme letters in %s\n", text);
        }
    } else if (!sam_parse_recited(text, &phonemes)) return false;
    capture_start = cache_used;
    capturing = cache_pcm && index == segment_count && index < CACHE_SEGMENTS;
    capture_overflow = false;
    audio_pwm_begin(voice.bit_depth);
    bool okay = sam_render(&phonemes, voice.speed, voice.pitch, voice.mouth,
                           voice.throat, pcm_sink, NULL);
    if (okay && !cancelled) okay = audio_pwm_end();
    else audio_pwm_stop();
    if (capturing) {
        if (okay && !cancelled && !capture_overflow) {
            key.offset = capture_start;
            key.count = cache_used - capture_start;
            segments[segment_count++] = key;
        } else cache_used = capture_start;
    }
    capturing = false;
    return okay && !cancelled;
}
static unsigned punctuation_pause(char c) {
    switch (c) {
        case '.': case ',': return 150;
        case '!': case '?': return 400;
        case ';': case ':': return 250;
        default: return 0;
    }
}
static bool pause_ms(unsigned milliseconds) {
    absolute_time_t until = make_timeout_time_ms(milliseconds);
    while (!cancelled && !time_reached(until)) service();
    return !cancelled;
}

static bool speak(const char *text, bool phonetic) {
    if (!text || busy) return false;
    size_t total = 0;
    while (text[total] && total < PVS_MAX_PHRASE) ++total;
    if (total == PVS_MAX_PHRASE) {
        printf("Speech input exceeds %u bytes\n", PVS_MAX_PHRASE - 1u);
        return false;
    }
    cache_pcm = pvs_sample_buffer(&cache_capacity);
    if (strcmp(cache_text, text) || cache_phonetic != phonetic) {
        memcpy(cache_text, text, total + 1);
        cache_phonetic = phonetic;
        cache_used = segment_count = 0;
    }
    segment_cursor = 0;
    busy = true;
    cancelled = false;
    bool okay = true;
    size_t at = 0;
    char word[PVS_MAX_PHRASE];
    while (at < total && okay && !cancelled) {
        while (at < total && isspace((unsigned char)text[at])) ++at;
        size_t start = at;
        while (at < total && !isspace((unsigned char)text[at])) ++at;
        size_t length = at - start;
        if (!length) break;
        memcpy(word, text + start, length);
        word[length] = '\0';
        if (word_callback) word_callback(word_context);
        if (cancelled) break;
        if (phonetic) {
            /* /H and /X are actual two-character SAM phonemes. Strip an
             * optional leading slash only when it is a prefix convention. */
            char *part = word;
            if (part[0] == '/' && toupper((unsigned char)part[1]) != 'H' &&
                toupper((unsigned char)part[1]) != 'X') ++part;
            okay = render_word(part, true);
        } else {
            /* Match SAM._split_token: punctuation separates subwords and adds
             * its pause without introducing another word-modulation update. */
            size_t begin = 0;
            for (size_t i = 0; i <= length && okay && !cancelled; ++i) {
                unsigned pause = punctuation_pause(word[i]);
                if (i != length && !pause) continue;
                word[i] = '\0';
                if (i > begin) {
                    okay = render_word(word + begin, false);
                    if (okay && pause) okay = pause_ms(pause);
                }
                begin = i + 1;
            }
        }
        service();
    }
    if (!okay && !cancelled) printf("SAM rejected malformed or oversized input\n");
    audio_pwm_stop();
    busy = false;
    return okay && !cancelled;
}

bool sam_pvs_say(const char *text) { return speak(text, false); }
bool sam_pvs_say_phonetic(const char *phonetic) { return speak(phonetic, true); }

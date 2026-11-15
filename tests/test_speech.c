#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sam_pvs.h"
#include "sam_engine.h"
#include "audio_pwm.h"
#include "storage.h"

static uint8_t sample_reserve[131072];
static size_t reserve_capacity = sizeof sample_reserve;
static uint32_t pcm_hash;
uint8_t *pvs_sample_buffer(size_t *capacity) {
    *capacity = reserve_capacity;
    return sample_reserve;
}
uint64_t test_time_us;
static unsigned words, begins, samples, services;
static unsigned cancel_at;
static bool playing;
static void (*audio_service)(void *);
static void *audio_context;
static uint32_t live_rate;
static uint8_t last_bits;
static sam_voice_t test_voice = {72, 96, 128, 128, 22050, 3};

void audio_pwm_init(uint32_t rate) { live_rate = rate; }
void audio_pwm_set_rate(uint32_t rate) { live_rate = rate; }
void audio_pwm_set_service_callback(void (*callback)(void *), void *context) {
    audio_service = callback; audio_context = context;
}
void audio_pwm_begin(uint8_t bits) { playing = true; ++begins; last_bits = bits; }
bool audio_pwm_write(const uint8_t *pcm, size_t count) {
    assert(playing && pcm && count <= 128);
    for (size_t i = 0; i < count; ++i) pcm_hash = pcm_hash * 16777619u ^ pcm[i];
    samples += (unsigned)count;
    if (audio_service) audio_service(audio_context);
    return playing;
}
bool audio_pwm_end(void) { bool okay = playing; playing = false; return okay; }
void audio_pwm_stop(void) { playing = false; }
void audio_pwm_print_status(void) {}

static void before_word(void *context) {
    (void)context;
    assert(sam_pvs_is_busy());
    ++words;
    sam_pvs_set_voice(&test_voice);
}
static void service(void *context) {
    (void)context;
    ++services;
    if (cancel_at && samples >= cancel_at) sam_pvs_stop();
}
static void reset(void) {
    words = begins = samples = services = cancel_at = 0;
    test_time_us = 0;
    pcm_hash = 2166136261u;
}
static bool count_pcm(const uint8_t *pcm, size_t count, void *context) {
    size_t *n = context;
    assert(pcm && count <= 128);
    *n += count;
    return *n < 1000000;
}
static uint32_t random_state = 1234567;
static uint32_t random_next(void) {
    random_state = random_state * 1664525u + 1013904223u;
    return random_state;
}

int main(void) {
    sam_pvs_init();
    sam_pvs_set_word_callback(before_word, NULL);
    sam_pvs_set_service_callback(service, NULL);
    reset();
    assert(sam_pvs_say("HELLO THERE"));
    assert(words == 2 && begins == 2 && samples > 1000 && services > 0);
    assert(last_bits == 3 && live_rate == 22050 && !sam_pvs_is_busy() && !playing);
    uint32_t first_hash = pcm_hash;
    unsigned first_samples = samples, hits = sam_pvs_cache_hits();
    reset();
    assert(sam_pvs_say("HELLO THERE"));
    assert(sam_pvs_cache_hits() == hits + 2);
    assert(samples == first_samples && pcm_hash == first_hash && words == 2);
    reset(); cancel_at = 256;
    assert(!sam_pvs_say("HELLO THERE"));
    reset();
    assert(sam_pvs_say("HELLO THERE"));
    assert(samples == first_samples && pcm_hash == first_hash);
    hits = sam_pvs_cache_hits();
    test_voice.speed = 100;
    reset(); assert(sam_pvs_say("HELLO THERE"));
    assert(sam_pvs_cache_hits() == hits && pcm_hash != first_hash);
    uint32_t changed_hash = pcm_hash;
    test_voice.sample_rate = 16000; test_voice.bit_depth = 8;
    reset(); assert(sam_pvs_say("HELLO THERE"));
    assert(sam_pvs_cache_hits() == hits + 2 && pcm_hash == changed_hash);
    assert(live_rate == 16000 && last_bits == 8);
    test_voice = (sam_voice_t){72, 96, 128, 128, 22050, 3};
    /* A tiny reservation must stream the whole word without overflow. */
    reserve_capacity = 128;
    memset(sample_reserve, 0xa5, sizeof sample_reserve);
    reset(); assert(sam_pvs_say("BOUNDARY"));
    assert(samples > 128);
    for (size_t i = 128; i < sizeof sample_reserve; ++i) assert(sample_reserve[i] == 0xa5);
    reserve_capacity = sizeof sample_reserve;


    reset();
    assert(sam_pvs_say("one,two...three!"));
    assert(words == 1 && begins == 3 && test_time_us >= 700000);
    reset();
    assert(sam_pvs_say("HOTEL ROBOTIC")); /* Reference reciter exception spellings. */
    assert(words == 2 && begins == 2);
    reset();
    assert(sam_pvs_say_phonetic("/HEH4LOW /AY4 AEM"));
    assert(words == 3 && begins == 3 && samples > 1000);
    assert(!sam_pvs_say_phonetic("AH$"));
    assert(!sam_pvs_say_phonetic("8AH"));
    assert(!sam_pvs_is_busy() && !playing);
    char long_text[PVS_MAX_PHRASE + 1];
    memset(long_text, 'A', sizeof long_text - 1);
    long_text[sizeof long_text - 1] = '\0';
    assert(!sam_pvs_say(long_text));
    reset();
    cancel_at = 256;
    assert(!sam_pvs_say("HELLO THERE"));
    assert(words == 1 && !sam_pvs_is_busy() && !playing);

    /* Exercise every supplied editable phrase through the actual wrapper,
     * reciter, parser and vendored C renderer with sanitizer instrumentation. */
    reset();
    for (unsigned bank = 1; bank <= 12; ++bank) {
        char path[64], line[2048];
        snprintf(path, sizeof path, "data/phrases_%u.txt", bank);
        FILE *file = fopen(path, "r");
        assert(file);
        while (fgets(line, sizeof line, file)) {
            phrase_t phrase;
            bool present;
            assert(storage_parse_phrase(line, &phrase, &present));
            if (!present) continue;
            bool okay = phrase.kind == PHRASE_PHONETIC ? sam_pvs_say_phonetic(phrase.text) : sam_pvs_say(phrase.text);
            if (!okay) fprintf(stderr, "Rejected factory phrase %s: %s\n", path, phrase.text);
            assert(okay);
        }
        assert(!ferror(file));
        fclose(file);
    }
    /* Valid phoneme mixtures stress short transitions, punctuation, sampled
     * consonants, extreme formants and repeated rendering without stale state. */
    static const char *const tokens[] = {
        "AH4", "IY8", "S", "Z", "SH", "/H", "N", "T", "R", "L", "CH",
        "DH", ".", "?", ",", "-", "B", "D", "G", "K", "P", "UX"
    };
    for (unsigned iteration = 0; iteration < 600; ++iteration) {
        char text[256] = {0};
        size_t used = 0;
        unsigned count = 1 + random_next() % 35;
        for (unsigned i = 0; i < count; ++i) {
            const char *token = tokens[random_next() % (sizeof tokens / sizeof tokens[0])];
            size_t n = strlen(token);
            memcpy(text + used, token, n); used += n;
        }
        sam_phonemes_t p;
        assert(sam_parse(text, &p));
        size_t count_pcm_bytes = 0;
        bool okay = sam_render(&p, (uint8_t)(1 + random_next() % 255),
            (uint8_t)random_next(), (uint8_t)random_next(), (uint8_t)random_next(),
            count_pcm, &count_pcm_bytes);
        assert(okay || count_pcm_bytes >= 1000000);
    }
    puts("Speech wrapper, factory corpus, cancellation and 600 renderer sanitizer cases passed");
}

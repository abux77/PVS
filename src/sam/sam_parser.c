/* Bounded port of reference_python/sam/phonemes.py. */
#include "sam_engine.h"
#include <string.h>
#include "sam_tables.h"
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define VOWEL 0x80
#define VOICED 0x40
#define STOP 0x20
#define FRICATIVE 0x10
#define NASAL 0x08
#define LIQUID 0x04

static uint8_t flags(unsigned index) { return index < COUNT(PHONEME_FLAGS) ? PHONEME_FLAGS[index] : 0; }
static char upper(char c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
static bool insert(sam_phonemes_t *p, size_t at, uint8_t index, uint8_t stress, uint8_t length) {
    if (p->count >= SAM_PHONEME_CAPACITY - 1) return false;
    memmove(p->index+at+1,p->index+at,p->count-at);
    memmove(p->stress+at+1,p->stress+at,p->count-at);
    memmove(p->length+at+1,p->length+at,p->count-at);
    p->index[at] = index; p->stress[at] = stress; p->length[at] = length;
    ++p->count;
    return true;
}

static bool parse(const char *text, sam_phonemes_t *p, bool strict) {
    if (!text || !p) return false;
    memset(p,0,sizeof *p);
    for (size_t i = 0; text[i] && (unsigned char)text[i] != 0x9b;) {
        char c = upper(text[i]);
        if (c >= '1' && c <= '8') {
            if (!p->count) return false;
            p->stress[p->count-1] = (uint8_t)(c-'0');
            ++i;
            continue;
        }
        int index = -1;
        size_t width = 1;
        for (size_t n = 0; text[i+1] && n < COUNT(PHONEME_NAMES); ++n) {
            if (PHONEME_NAMES[n][1] != '*' && PHONEME_NAMES[n][0] == c &&
                PHONEME_NAMES[n][1] == upper(text[i+1])) { index = (int)n; width = 2; break; }
        }
        if (index < 0) {
            for (size_t n = 0; n < COUNT(PHONEME_NAMES); ++n) {
                if (PHONEME_NAMES[n][1] == '*' && PHONEME_NAMES[n][0] == c && c != '*') { index = (int)n; break; }
            }
        }
        if (index < 0) {
            if (strict) return false;
            ++i; /* Match Python for trusted reciter-generated phonemes. */
            continue;
        }
        if (!insert(p,p->count,(uint8_t)index,0,0)) return false;
        i += width;
    }
    for (size_t i = 0; i < p->count;) {
        unsigned idx = p->index[i];
        unsigned next = i+1 < p->count ? p->index[i+1] : 255;
        unsigned f = flags(idx);
        if (!idx) { ++i; continue; }
        if (idx >= 48 && idx <= 53) {
            if (!insert(p,i+1,idx <= 50 ? 21 : 20,p->stress[i],0)) return false;
            i += 2; continue;
        }
        if (idx >= 78 && idx <= 80) {
            p->index[i] = 13;
            if (!insert(p,i+1,idx == 78 ? 24 : idx == 79 ? 27 : 28,p->stress[i],0)) return false;
            i += 2; continue;
        }
        if (idx == 42 || idx == 44) {
            if (!insert(p,i+1,(uint8_t)(idx+1),p->stress[i],0)) return false;
            i += 2; continue;
        }
        if (idx == 32 && (next == 66 || next == 69 || next == 72 || next == 75)) p->index[i+1] -= 12;
        if ((idx == 69 || idx == 57) && next == 23) { p->index[i] = idx == 69 ? 42 : 44; ++i; continue; }
        if ((f & VOWEL) && (next == 23 || next == 24)) p->index[i+1] = next == 23 ? 18 : 19;
        if ((idx == 60 || idx == 72) && next >= 5 && next <= 8) p->index[i] += 3;
        idx = p->index[i];
        if (idx == 54 || idx == 57 || idx == 60 || idx == 63 || idx == 66 || idx == 69 || idx == 72 || idx == 75) {
            uint8_t stress = p->stress[i];
            if (!insert(p,i+1,(uint8_t)(idx+1),stress,0) || !insert(p,i+2,(uint8_t)(idx+2),stress,0)) return false;
            i += 3; continue;
        }
        ++i;
    }
    for (size_t i = 0; i+1 < p->count; ++i) {
        unsigned f = flags(p->index[i]);
        if (!(f & VOWEL) && (f & VOICED) && (flags(p->index[i+1]) & VOWEL) && p->stress[i+1]) p->stress[i] = p->stress[i+1]+1;
    }
    for (size_t i = 0; i < p->count; ++i) {
        unsigned idx = p->index[i];
        p->length[i] = idx >= COUNT(PHONEME_LENGTH) ? 6 :
            p->stress[i] && idx < COUNT(PHONEME_STRESSED_LENGTH) ? PHONEME_STRESSED_LENGTH[idx] : PHONEME_LENGTH[idx];
    }
    for (size_t i = 0; i+1 < p->count; ++i) {
        unsigned f = flags(p->index[i]), next = p->index[i+1], nf = flags(next);
        unsigned length = p->length[i];
        if (next >= 1 && next <= 3 && (f & (VOICED | FRICATIVE))) length = length*3/2+1;
        if ((f & VOWEL) && (next == 18 || next == 19) && i+2 < p->count && !(flags(p->index[i+2]) & VOWEL)) length = length > 1 ? length-1 : 1;
        if ((f & VOWEL) && (nf & STOP) && !(nf & VOICED)) length -= length >> 3;
        if ((f & VOWEL) && (nf & VOICED) && !(nf & VOWEL)) length = length*5/4+1;
        if ((f & NASAL) && (nf & STOP)) { length = 5; p->length[i+1] = 6; }
        if ((f & STOP) && (nf & STOP)) length = (length >> 1)+1;
        if ((f & LIQUID) && next >= 48 && next <= 53) length = length > 2 ? length-2 : 1;
        p->length[i] = (uint8_t)length;
    }
    unsigned total = 0;
    for (size_t i = 0; i < p->count; ++i) {
        unsigned idx = p->index[i];
        total += p->length[i];
        if (total > 232) {
            if (!insert(p,i,31,0,5)) return false;
            total = 0;
            ++i;
        }
        if (idx >= 1 && idx <= 3) total = 0;
    }
    p->index[p->count] = 255;
    return true;
}

bool sam_parse(const char *text, sam_phonemes_t *p) { return parse(text, p, true); }
bool sam_parse_recited(const char *text, sam_phonemes_t *p) { return parse(text, p, false); }

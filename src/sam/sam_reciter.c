/* Bounded C port of reference_python/sam/reciter.py, including PVS exceptions. */
#include "sam_engine.h"
#include <string.h>
#include "sam_rules.h"
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static bool alpha(char c) { return c >= 'A' && c <= 'Z'; }
static bool member(char c, const char *set) { return c && strchr(set, c) != NULL; }
static bool vowel(char c) { return member(c, "AEIOUY"); }
static bool consonant(char c) { return alpha(c) && !vowel(c); }
static bool alphabetic(char c) { return alpha(c) || c == '\''; }
static char upper(char c) { return c >= 'a' && c <= 'z' ? c - ('a'-'A') : c; }

static bool pattern_char(char c, char pattern) {
    switch (pattern) {
    case ' ': return !alphabetic(c);
    case '#': return vowel(c);
    case '.': return member(c,"BDGJLMNRVWZ");
    case '&': return member(c,"SCGZXJ");
    case '@': return member(c,"TSRDLZNJ");
    case '^': return consonant(c);
    case '+': return member(c,"EIY");
    default: return c == pattern;
    }
}

static bool prefix_matches(const char *text, size_t pos, const char *pattern) {
    int t = (int)pos - 1;
    for (int i = (int)strlen(pattern) - 1; i >= 0; --i) {
        char p = pattern[i];
        if (p == ':') { while (t >= 0 && consonant(text[t])) --t; }
        else if (p == ' ' && t < 0) continue;
        else if (t < 0 || !pattern_char(text[t--], p)) return false;
    }
    return true;
}

static bool suffix_matches(const char *text, size_t pos, const char *pattern) {
    size_t n = strlen(text);
    static const char *const endings[] = {"ING","ER","ES","ED","ELY","EFUL","ENESS"};
    for (size_t i = 0; pattern[i]; ++i) {
        char p = pattern[i];
        if (p == ':') { while (pos < n && consonant(text[pos])) ++pos; }
        else if (p == '%') {
            size_t j;
            for (j = 0; j < COUNT(endings); ++j) {
                size_t length = strlen(endings[j]);
                if (length <= n - pos && !memcmp(text + pos, endings[j], length)) { pos += length; break; }
            }
            if (j == COUNT(endings)) return false;
        } else if (p == ' ' && pos >= n) continue;
        else if (pos >= n || !pattern_char(text[pos++], p)) return false;
    }
    return true;
}

static bool append(char *output, size_t capacity, size_t *used, const char *value) {
    size_t n = strlen(value);
    if (n >= capacity - *used) return false;
    memcpy(output + *used, value, n);
    *used += n;
    output[*used] = 0;
    return true;
}

bool sam_recite(const char *input, char *output, size_t capacity) {
    if (!input || !output || !capacity) return false;
    output[0] = 0;
    char text[SAM_TEXT_CAPACITY + 3];
    size_t n = 0;
    while (input[n] && n < SAM_TEXT_CAPACITY) ++n;
    if (input[n]) return false;
    text[0] = ' ';
    for (size_t i = 0; i < n; ++i) {
        if ((unsigned char)input[i] >= 128) return false;
        text[i + 1] = upper(input[i]);
    }
    text[n+1] = ' ';
    text[n+2] = 0;
    size_t used = 0;
    for (size_t pos = 1; pos <= n;) {
        char c = text[pos];
        if (alpha(c)) {
            size_t end = pos;
            while (alphabetic(text[end])) ++end;
            size_t stripped = end;
            while (stripped > pos && text[stripped-1] == '\'') --stripped;
            bool found = false;
            for (unsigned pass = 0; pass < 2 && !found; ++pass) {
                size_t word_end = pass ? stripped : end;
                if (pass && end == stripped) break;
                for (size_t e = 0; e < COUNT(EXCEPTIONS); ++e) {
                    if (strlen(EXCEPTIONS[e].word) == word_end - pos &&
                        !memcmp(text + pos, EXCEPTIONS[e].word, word_end-pos)) {
                        if (!append(output,capacity,&used,EXCEPTIONS[e].phonemes)) return false;
                        pos = word_end;
                        found = true;
                        break;
                    }
                }
            }
            if (found) continue;
        }
        if (c >= '0' && c <= '9') {
            if (!append(output,capacity,&used,RULES_NUM[c-'0'].replacement)) return false;
            ++pos;
            continue;
        }
        if (c == ' ') {
            if (!append(output,capacity,&used," ")) return false;
            ++pos;
            continue;
        }
        sam_rule_group_t group = alpha(c) ? RULE_GROUPS[c-'A'] :
            (sam_rule_group_t){RULES_PUNCT, COUNT(RULES_PUNCT)};
        bool found = false;
        for (size_t r = 0; r < group.count; ++r) {
            const sam_rule_t *rule = &group.rules[r];
            size_t m = strlen(rule->match);
            /* The Python '(' rule has an empty match; do not permit zero progress. */
            if (!m || m > n+2-pos || memcmp(text+pos,rule->match,m)) continue;
            if (!prefix_matches(text,pos,rule->prefix) || !suffix_matches(text,pos+m,rule->suffix)) continue;
            if (!append(output,capacity,&used,rule->replacement)) return false;
            pos += m;
            found = true;
            break;
        }
        if (!found) ++pos;
    }
    return true;
}

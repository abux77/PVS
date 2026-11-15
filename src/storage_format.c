#include "storage.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) ++s;
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
    return s;
}

bool storage_parse_phrase(char *line, phrase_t *out, bool *present) {
    if (!line || !out || !present) return false;
    *present = false;
    char *text = trim(line);
    if (!*text || *text == '#') return true;
    out->kind = PHRASE_SAY;
    if (text[1] == ':' && (toupper((unsigned char)*text) == 'P' ||
                           toupper((unsigned char)*text) == 'S')) {
        out->kind = toupper((unsigned char)*text) == 'P' ? PHRASE_PHONETIC : PHRASE_SAY;
        text = trim(text + 2);
    }
    size_t n = strlen(text);
    if (!n) return true;
    if (n >= sizeof(out->text)) return false;
    memcpy(out->text, text, n + 1);
    *present = true;
    return true;
}

static unsigned csv_numbers(char *line, long *values, unsigned capacity) {
    char *p = trim(line);
    if (!*p || *p == '#') return 0;
    unsigned count = 0;
    while (*p && count < capacity) {
        char *end;
        errno = 0;
        long value = strtol(p, &end, 10);
        if (p == end || errno == ERANGE) return 0;
        while (isspace((unsigned char)*end)) ++end;
        if (*end && *end != ',') return 0;
        values[count++] = value;
        if (!*end) return count;
        p = end + 1;
        if (!*p) return 0;
    }
    return 0; /* Extra columns cannot be silently accepted. */
}

static bool parse_voice(const long *v, long bits, sam_voice_t *out) {
    /* Edited files cannot wrap integer conversions or create zero-speed loops. */
    if (v[0] < 1 || v[0] > 255 || v[1] < 0 || v[1] > 255 ||
        v[2] < 0 || v[2] > 255 || v[3] < 0 || v[3] > 255 ||
        v[4] < PVS_MIN_RATE || v[4] > PVS_MAX_RATE) return false;
    out->speed = (uint8_t)v[0];
    out->pitch = (uint8_t)v[1];
    out->mouth = (uint8_t)v[2];
    out->throat = (uint8_t)v[3];
    out->sample_rate = (uint32_t)v[4];
    out->bit_depth = (uint8_t)(bits < 1 ? 1 : bits > 8 ? 8 : bits);
    return true;
}

/* Consume a printed key before the numeric CSV fields. In particular #,
 * is a key row, whereas # followed by other text is a comment. Legacy
 * numeric 10/11/12 are accepted as aliases for star/zero/hash. */
static bool key_column(char *line, unsigned *slot, char **fields) {
    char *p = trim(line);
    char *comma = strchr(p, ',');
    if (!comma) return false;
    size_t n = (size_t)(comma - p);
    while (n && isspace((unsigned char)p[n - 1])) --n;
    if (n == 1) {
        if (*p >= '1' && *p <= '9') *slot = (unsigned)(*p - '1');
        else if (*p == '*') *slot = 9;
        else if (*p == '0') *slot = 10;
        else if (*p == '#') *slot = 11;
        else return false;
    } else if (n == 2 && p[0] == '1' && p[1] >= '0' && p[1] <= '2') {
        *slot = 9u + (unsigned)(p[1] - '0');
    } else return false;
    *fields = comma + 1;
    return true;
}

bool storage_parse_preset(char *line, unsigned *slot, sam_voice_t *out) {
    long v[6];
    char *fields;
    if (!line || !slot || !out || !key_column(line, slot, &fields)) return false;
    unsigned count = csv_numbers(fields, v, 6);
    if (count != 5 && count != 6) return false;
    return parse_voice(v, count == 6 ? v[5] : 8, out);
}

bool storage_parse_lock(char *line, unsigned *slot, phrase_lock_t *out) {
    long v[7];
    char *fields;
    if (!line || !slot || !out || !key_column(line, slot, &fields)) return false;
    if (csv_numbers(fields, v, 7) != 7) return false;
    unsigned flags = (unsigned long)v[0] & (LOCK_PITCH | LOCK_VOICE | LOCK_SAMPLE);
    if (!flags || !parse_voice(v + 1, v[6], &out->voice)) return false;
    out->flags = (uint8_t)flags;
    return true;
}

bool storage_parse_startup(char *line, unsigned *bank, unsigned *preset) {
    long v[2];
    if (!line || !bank || !preset) return false;
    *bank = 1;
    *preset = 0;
    /* The Python reader consumes only the first two columns. */
    char *first_comma = strchr(line, ',');
    if (first_comma) {
        char *second_comma = strchr(first_comma + 1, ',');
        if (second_comma) *second_comma = '\0';
    }
    unsigned count = csv_numbers(line, v, 2);
    if (!count) return false;
    if (v[0] >= 1 && v[0] <= 12) *bank = (unsigned)v[0];
    if (count >= 2 && v[1] >= 0 && v[1] <= 12) *preset = (unsigned)v[1];
    return true;
}

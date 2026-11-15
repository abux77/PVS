#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pvs_config.h"
#include "sam_pvs.h"
typedef enum { PHRASE_SAY, PHRASE_PHONETIC } phrase_kind_t;
typedef struct { phrase_kind_t kind; char text[PVS_MAX_PHRASE]; } phrase_t;
typedef struct { phrase_t item[PVS_MAX_PHRASES]; unsigned count; } phrase_bank_t;
enum { LOCK_PITCH = 1, LOCK_VOICE = 2, LOCK_SAMPLE = 4 };
typedef struct { uint8_t flags; sam_voice_t voice; } phrase_lock_t;

/* Filesystem operations run on core 0 while audio is idle. */
bool storage_init(void);
bool storage_load_bank(unsigned bank, phrase_bank_t *out);
bool storage_load_presets(sam_voice_t presets[12], uint16_t *valid_mask);
bool storage_save_presets(const sam_voice_t presets[12], uint16_t valid_mask);
bool storage_load_locks(unsigned bank, phrase_lock_t locks[12]);
bool storage_save_locks(unsigned bank, const phrase_lock_t locks[12]);
bool storage_load_startup(unsigned *bank, unsigned *preset);
bool storage_save_startup(unsigned bank, unsigned preset);

/* Console service requests editing; pvs calls begin_edit after audio stops.
 * Host eject must precede remount. No forced mount command is provided. */
void storage_console_service(void);
void storage_set_status_callback(void (*callback)(void));
bool storage_edit_requested(void);
bool storage_format_requested(void);
/* Destructive factory reset of reserved storage; idle core 0 only, never USB-owned. */
bool storage_format_factory(void);
bool storage_begin_edit(void);
bool storage_edit_active(void);
bool storage_reload_pending(void); /* Consumes successful-remount notification. */
bool storage_is_ready(void);
typedef struct {
    unsigned mount_result; /* FatFs FRESULT: 0=OK, 1=I/O, 3=not ready, 13=no FAT. */
    bool flash_available, blank_checked, initially_blank, provision_attempted;
} storage_diagnostics_t;
storage_diagnostics_t storage_get_diagnostics(void);

/* Bounded parsers independent of Pico/FatFs. Input lines may be modified.
 * Bad numeric rows are ignored; oversized phrases fail the whole bank load. */
bool storage_parse_phrase(char *line, phrase_t *out, bool *present);
bool storage_parse_preset(char *line, unsigned *slot, sam_voice_t *out);
bool storage_parse_lock(char *line, unsigned *slot, phrase_lock_t *out);
bool storage_parse_startup(char *line, unsigned *bank, unsigned *preset);

/* Idle core 0 only; refuses while USB host owns storage. */
bool storage_prepare_bootsel(void);

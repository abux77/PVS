#include "keypad.h"
#include "storage.h"
#include "flash_disk.h"
#include "ff.h"
#include "diskio.h"
#include "storage_defaults.h"
#include <stdio.h>
#include <string.h>

static FATFS filesystem;
static bool disk_available;
static bool mounted;
static bool editing;
static bool host_medium;
static bool remount_requested;
static bool reloaded;
static phrase_bank_t bank_work;
static storage_diagnostics_t diagnostics;

storage_diagnostics_t storage_get_diagnostics(void) { return diagnostics; }

/* Single-core, exclusive owners: firmware calls FatFs while mounted; USB calls
 * flash_disk directly only after unmount. Cache sync precedes each handoff. */
DSTATUS disk_initialize(BYTE drive) {
    return drive || !disk_available ? STA_NOINIT : 0;
}
DSTATUS disk_status(BYTE drive) {
    return disk_initialize(drive);
}
DRESULT disk_read(BYTE drive, BYTE *buffer, LBA_t sector, UINT count) {
    if (drive || !count || sector >= PVS_DISK_SECTOR_COUNT ||
        count > PVS_DISK_SECTOR_COUNT - sector) return RES_PARERR;
    return flash_disk_read((uint32_t)sector * PVS_DISK_SECTOR_SIZE, buffer,
                           (size_t)count * PVS_DISK_SECTOR_SIZE) ? RES_OK : RES_ERROR;
}
DRESULT disk_write(BYTE drive, const BYTE *buffer, LBA_t sector, UINT count) {
    if (drive || !count || sector >= PVS_DISK_SECTOR_COUNT ||
        count > PVS_DISK_SECTOR_COUNT - sector) return RES_PARERR;
    return flash_disk_write((uint32_t)sector * PVS_DISK_SECTOR_SIZE, buffer,
                            (size_t)count * PVS_DISK_SECTOR_SIZE) ? RES_OK : RES_ERROR;
}
DRESULT disk_ioctl(BYTE drive, BYTE command, void *buffer) {
    if (drive || !disk_available) return RES_NOTRDY;
    switch (command) {
    case CTRL_SYNC: return flash_disk_sync() ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT:
        if (!buffer) return RES_PARERR;
        *(LBA_t *)buffer = PVS_DISK_SECTOR_COUNT;
        return RES_OK;
    case GET_SECTOR_SIZE:
        if (!buffer) return RES_PARERR;
        *(WORD *)buffer = PVS_DISK_SECTOR_SIZE;
        return RES_OK;
    case GET_BLOCK_SIZE:
        if (!buffer) return RES_PARERR;
        *(DWORD *)buffer = 4096 / PVS_DISK_SECTOR_SIZE;
        return RES_OK;
    default: return RES_PARERR;
    }
}

static const storage_default_t *find_default(const char *name) {
    for (size_t i = 0; i < sizeof(storage_defaults) / sizeof(storage_defaults[0]); ++i)
        if (!strcmp(name, storage_defaults[i].name)) return &storage_defaults[i];
    return NULL;
}

static bool write_file(const char *name, const char *text, size_t size) {
    FIL file;
    if (f_open(&file, name, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return false;
    UINT written = 0;
    FRESULT result = f_write(&file, text, (UINT)size, &written);
    FRESULT closed = f_close(&file);
    return result == FR_OK && written == size && closed == FR_OK;
}

/* Temporary+backup replacement retains the previous text file through ordinary
 * interrupted saves. FAT itself is not journalled: eject and avoid power loss
 * during writes. Existing backups are recovered when the final file is absent. */
static bool replace_file(const char *name, const char *text, size_t size) {
    if (!mounted || editing) return false;
    char temporary[64], backup[64];
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", name) >= (int)sizeof(temporary) ||
        snprintf(backup, sizeof(backup), "%s.bak", name) >= (int)sizeof(backup)) return false;
    FILINFO info;
    if (f_stat(name, &info) == FR_NO_FILE) {
        FRESULT recovered = f_rename(backup, name);
        if (recovered != FR_OK && recovered != FR_NO_FILE) return false;
    }
    if (!write_file(temporary, text, size)) return false;
    FRESULT result = f_unlink(backup);
    if (result != FR_OK && result != FR_NO_FILE) return false;
    result = f_rename(name, backup);
    bool had_original = result == FR_OK;
    if (!had_original && result != FR_NO_FILE) return false;
    if (f_rename(temporary, name) != FR_OK) {
        if (had_original) (void)f_rename(backup, name);
        return false;
    }
    if (had_original && f_unlink(backup) != FR_OK) return false;
    return flash_disk_sync();
}

static FRESULT provision_factory_files(void) {
    /* FatFs chooses FAT12 for this 1 MiB superfloppy. Long filenames retain
     * the exact phrases_N.txt/startup_state.txt names from MicroPython. */
    uint8_t work[512];
    MKFS_PARM options = {.fmt = FM_FAT | FM_SFD, .n_fat = 2,
                         .align = 8, .n_root = 128, .au_size = 512};
    FRESULT result = f_mkfs("", &options, work, sizeof(work));
    if (result == FR_OK) result = f_mount(&filesystem, "", 1);
    if (result == FR_OK) {
        result = f_setlabel("PVS");
        for (size_t i = 0; result == FR_OK &&
             i < sizeof(storage_defaults) / sizeof(storage_defaults[0]); ++i) {
            if (!write_file(storage_defaults[i].name, storage_defaults[i].text,
                            storage_defaults[i].size)) result = FR_DISK_ERR;
        }
        /* Prevent desktop indexing from consuming the small instrument disk. */
        if (result == FR_OK && !write_file(".metadata_never_index", "", 0))
            result = FR_DISK_ERR;
    }
    if (result == FR_OK && !flash_disk_sync()) result = FR_DISK_ERR;
    return result;
}

bool storage_init(void) {
    disk_available = flash_disk_init();
    diagnostics = (storage_diagnostics_t){.mount_result = FR_NOT_READY,
                                         .flash_available = disk_available};
    mounted = false;
    editing = host_medium = remount_requested = reloaded = false;
    if (!disk_available) {
        printf("Storage: reserved flash unavailable; factory fallback\n");
        return false;
    }
    FRESULT result = f_mount(&filesystem, "", 1);
    if (result != FR_OK) {
        diagnostics.blank_checked = true;
        diagnostics.initially_blank = flash_disk_is_blank();
    }
    if (result != FR_OK && diagnostics.initially_blank) {
        diagnostics.provision_attempted = true;
        printf("Storage: provisioning blank 1 MiB FAT volume\n");
        result = provision_factory_files();
    }
    diagnostics.mount_result = result;
    mounted = result == FR_OK;
    if (!mounted) (void)f_mount(NULL, "", 0);
    printf("Storage: %s (FAT result %u, %u KiB); CDC: edit, status, help\n",
           mounted ? "firmware owns flash" : "factory fallback; edit mode can repair disk",
           (unsigned)result, PVS_STORAGE_BYTES / 1024);
    return mounted;
}

bool storage_format_factory(void) {
    /* Called only by the idle control loop after an explicit console request.
     * The block driver confines every write to the reserved final MiB. */
    if (editing || !disk_available) {
        printf("Format refused: eject USB edit disk first; flash must be available\n");
        return false;
    }
    if (!flash_disk_sync() || f_mount(NULL, "", 0) != FR_OK) return false;
    mounted = false;
    printf("Formatting PVS storage and installing factory files...\n");
    FRESULT result = provision_factory_files();
    diagnostics.mount_result = result;
    mounted = result == FR_OK;
    if (!mounted) (void)f_mount(NULL, "", 0);
    reloaded = true;
    printf("PVS format %s (FAT result %u)\n", mounted ? "complete" : "FAILED", (unsigned)result);
    return mounted;
}

bool storage_is_ready(void) { return mounted && !editing; }
bool storage_edit_active(void) { return editing; }
bool storage_host_ready(void) { return editing && host_medium; }

bool storage_begin_edit(void) {
    if (editing) return true;
    if (!disk_available || !flash_disk_sync()) return false;
    if (f_mount(NULL, "", 0) != FR_OK) return false;
    mounted = false;
    editing = host_medium = true;
    printf("USB EDIT: host owns PVS disk. Edit, save, then eject to resume speech.\n");
    return true;
}

bool storage_host_eject(void) {
    if (!storage_host_ready() || !flash_disk_sync()) return false;
    host_medium = false;
    remount_requested = true;
    return true;
}

void storage_service_remount(void) {
    if (!remount_requested) return;
    remount_requested = false;
    FRESULT result = f_mount(&filesystem, "", 1);
    diagnostics.mount_result = result;
    mounted = result == FR_OK;
    editing = false;
    reloaded = true; /* Reload factory fallback too if host left an invalid disk. */
    if (!mounted) (void)f_mount(NULL, "", 0);
    printf("USB eject: firmware %s (FAT result %u)\n",
           mounted ? "remounted files" : "using factory fallback", (unsigned)result);
}

bool storage_reload_pending(void) {
    bool result = reloaded;
    reloaded = false;
    return result;
}

typedef struct {
    FIL file;
    const char *fallback;
    bool opened;
    bool error;
} text_reader_t;

static bool reader_open(text_reader_t *reader, const char *name) {
    memset(reader, 0, sizeof(*reader));
    if (editing) return false;
    if (mounted) {
        FRESULT result = f_open(&reader->file, name, FA_READ);
        if (result == FR_NO_FILE) {
            char backup[64];
            if (snprintf(backup, sizeof(backup), "%s.bak", name) >= (int)sizeof(backup))
                return false;
            /* Reading the backup is sufficient; the next save restores names. */
            result = f_open(&reader->file, backup, FA_READ);
        }
        reader->opened = result == FR_OK;
        return reader->opened;
    }
    const storage_default_t *entry = find_default(name);
    if (!entry) return false;
    reader->fallback = entry->text;
    return true;
}

static int reader_char(text_reader_t *reader) {
    if (reader->fallback) {
        unsigned char c = (unsigned char)*reader->fallback;
        if (!c) return -1;
        ++reader->fallback;
        return c;
    }
    unsigned char c;
    UINT got;
    if (f_read(&reader->file, &c, 1, &got) != FR_OK) {
        reader->error = true;
        return -1;
    }
    return got ? c : -1;
}

/* Return 1 for a line, 0 for EOF, -1 for a rejected overlong/binary line.
 * Discard the complete bad line so it can never become another button row. */
static int reader_line(text_reader_t *reader, char *line, size_t capacity) {
    size_t length = 0;
    bool overflow = false;
    int c;
    while ((c = reader_char(reader)) >= 0 && c != '\n') {
        if (!c || length + 1 >= capacity) overflow = true;
        else line[length++] = (char)c;
    }
    line[length] = '\0';
    if (overflow) return -1;
    return length || c == '\n' ? 1 : 0;
}

static bool reader_close(text_reader_t *reader) {
    if (reader->opened && f_close(&reader->file) != FR_OK) return false;
    return !reader->error;
}

bool storage_load_bank(unsigned bank, phrase_bank_t *out) {
    if (!out || bank < 1 || bank > 12) return false;
    char name[32], line[PVS_MAX_PHRASE + 8];
    snprintf(name, sizeof(name), "phrases_%u.txt", bank);
    text_reader_t reader;
    if (!reader_open(&reader, name)) return false;
    memset(&bank_work, 0, sizeof(bank_work));
    bool valid = true;
    int status;
    while ((status = reader_line(&reader, line, sizeof(line)))) {
        if (status < 0) { valid = false; break; }
        phrase_t phrase;
        bool present;
        if (!storage_parse_phrase(line, &phrase, &present)) { valid = false; break; }
        if (present) {
            if (bank_work.count >= PVS_MAX_PHRASES) { valid = false; break; }
            bank_work.item[bank_work.count++] = phrase;
        }
    }
    if (!reader_close(&reader)) valid = false;
    if (valid) *out = bank_work;
    else printf("Storage: rejected malformed/oversized bank %u; previous bank retained\n", bank);
    return valid;
}

bool storage_load_presets(sam_voice_t presets[12], uint16_t *valid_mask) {
    if (!presets || !valid_mask || editing) return false;
    *valid_mask = 0;
    memset(presets, 0, 12 * sizeof(*presets));
    text_reader_t reader;
    if (!reader_open(&reader, "presets.txt")) return false;
    char line[160];
    int status;
    while ((status = reader_line(&reader, line, sizeof(line)))) {
        sam_voice_t voice;
        unsigned slot;
        if (status > 0 && storage_parse_preset(line, &slot, &voice)) {
            presets[slot] = voice;
            *valid_mask |= (uint16_t)(1u << slot);
        }
    }
    return reader_close(&reader);
}

bool storage_save_presets(const sam_voice_t presets[12], uint16_t valid_mask) {
    if (!presets) return false;
    char text[1024];
    int used = snprintf(text, sizeof(text), "# key,speed,pitch,mouth,throat,sample_rate,bit_depth\n");
    for (unsigned i = 0; i < 12; ++i) {
        if (!(valid_mask & (1u << i))) continue;
        const sam_voice_t *v = &presets[i];
        int n = snprintf(text + used, sizeof(text) - (size_t)used,
                         "%c,%u,%u,%u,%u,%lu,%u\n", keypad_label(i), v->speed, v->pitch,
                         v->mouth, v->throat, (unsigned long)v->sample_rate, v->bit_depth);
        if (n < 0 || n >= (int)sizeof(text) - used) return false;
        used += n;
    }
    return replace_file("presets.txt", text, (size_t)used);
}

bool storage_load_locks(unsigned bank, phrase_lock_t locks[12]) {
    if (!locks || bank < 1 || bank > 12 || editing) return false;
    memset(locks, 0, 12 * sizeof(*locks));
    char name[32], line[160];
    snprintf(name, sizeof(name), "locks_%u.txt", bank);
    text_reader_t reader;
    if (!reader_open(&reader, name)) return true; /* Missing lock files mean no locks. */
    int status;
    while ((status = reader_line(&reader, line, sizeof(line)))) {
        phrase_lock_t lock;
        unsigned slot;
        if (status > 0 && storage_parse_lock(line, &slot, &lock)) locks[slot] = lock;
    }
    return reader_close(&reader);
}

bool storage_save_locks(unsigned bank, const phrase_lock_t locks[12]) {
    if (!locks || bank < 1 || bank > 12) return false;
    char name[32], text[1024];
    snprintf(name, sizeof(name), "locks_%u.txt", bank);
    int used = snprintf(text, sizeof(text), "# key,flags,speed,pitch,mouth,throat,sample_rate,bit_depth\n");
    for (unsigned i = 0; i < 12; ++i) {
        if (!locks[i].flags) continue;
        const sam_voice_t *v = &locks[i].voice;
        int n = snprintf(text + used, sizeof(text) - (size_t)used,
                         "%c,%u,%u,%u,%u,%u,%lu,%u\n", keypad_label(i), locks[i].flags,
                         v->speed, v->pitch, v->mouth, v->throat,
                         (unsigned long)v->sample_rate, v->bit_depth);
        if (n < 0 || n >= (int)sizeof(text) - used) return false;
        used += n;
    }
    return replace_file(name, text, (size_t)used);
}

bool storage_load_startup(unsigned *bank, unsigned *preset) {
    if (!bank || !preset || editing) return false;
    *bank = 1;
    *preset = 0;
    text_reader_t reader;
    if (!reader_open(&reader, "startup_state.txt")) return false;
    char line[64];
    bool valid = reader_line(&reader, line, sizeof(line)) > 0 &&
                 storage_parse_startup(line, bank, preset);
    return reader_close(&reader) && valid;
}

bool storage_save_startup(unsigned bank, unsigned preset) {
    if (bank < 1 || bank > 12 || preset > 12) return false;
    /* Avoid a flash erase when the selected bank/preset did not change. */
    unsigned previous_bank, previous_preset;
    if (storage_load_startup(&previous_bank, &previous_preset) &&
        bank == previous_bank && preset == previous_preset) return storage_is_ready();
    char text[32];
    int length = snprintf(text, sizeof(text), "%u,%u\n", bank, preset);
    return length > 0 && length < (int)sizeof(text) &&
           replace_file("startup_state.txt", text, (size_t)length);
}


bool storage_prepare_bootsel(void) {
    if (editing) return false;
    if (disk_available && !flash_disk_sync()) return false;
    if (f_mount(NULL, "", 0) != FR_OK) return false;
    mounted = false;
    return true;
}

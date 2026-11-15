/* Native tests exercise the real FatFs backend with a bounded fake flash disk. */
#include "storage.h"
#include "flash_disk.h"
#include "ff.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t disk[PVS_STORAGE_BYTES];
static unsigned writes;
bool flash_disk_init(void) { return true; }
bool flash_disk_is_blank(void) {
    for (size_t i = 0; i < sizeof(disk); ++i) if (disk[i] != 0xff) return false;
    return true;
}
bool flash_disk_read(uint32_t offset, void *buffer, size_t size) {
    if (offset > sizeof(disk) || size > sizeof(disk) - offset) return false;
    memcpy(buffer, disk + offset, size);
    return true;
}
bool flash_disk_write(uint32_t offset, const void *buffer, size_t size) {
    if (offset > sizeof(disk) || size > sizeof(disk) - offset) return false;
    memcpy(disk + offset, buffer, size);
    ++writes;
    return true;
}
bool flash_disk_sync(void) { return true; }

static void fixture(const char *path, const char *text) {
    FIL file;
    UINT wrote;
    assert(f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
    assert(f_write(&file, text, (UINT)strlen(text), &wrote) == FR_OK);
    assert(wrote == strlen(text));
    assert(f_close(&file) == FR_OK);
}
static char *mutable_line(const char *text) {
    static char line[1024];
    assert(strlen(text) < sizeof(line));
    memcpy(line, text, strlen(text) + 1);
    return line;
}
static void parsers(void) {
    phrase_t phrase;
    bool present;
    assert(storage_parse_phrase(mutable_line("  p: AY4 AEM  \r"), &phrase, &present));
    assert(present && phrase.kind == PHRASE_PHONETIC && !strcmp(phrase.text, "AY4 AEM"));
    assert(storage_parse_phrase(mutable_line("Plain text"), &phrase, &present));
    assert(present && phrase.kind == PHRASE_SAY && !strcmp(phrase.text, "Plain text"));
    assert(storage_parse_phrase(mutable_line("X:normal text"), &phrase, &present));
    assert(!strcmp(phrase.text, "X:normal text"));
    assert(storage_parse_phrase(mutable_line(" # comment"), &phrase, &present) && !present);
    assert(storage_parse_phrase(mutable_line("S:  "), &phrase, &present) && !present);
    char too_long[PVS_MAX_PHRASE + 1];
    memset(too_long, 'A', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = 0;
    assert(!storage_parse_phrase(too_long, &phrase, &present));

    sam_voice_t voice;
    unsigned slot;
    assert(storage_parse_preset(mutable_line(" 12,72,96,128,128,22050 "), &slot, &voice));
    assert(slot == 11 && voice.bit_depth == 8 && voice.sample_rate == 22050);
    assert(storage_parse_preset(mutable_line("1,72,96,128,128,22050,-2"), &slot, &voice));
    assert(voice.bit_depth == 1);
    assert(storage_parse_preset(mutable_line("1,72,96,128,128,22050,99"), &slot, &voice));
    assert(voice.bit_depth == 8);
    const char *bad[] = {"A,72,96,128,128,22050,8", "13,72,96,128,128,22050,8",
        "1,0,96,128,128,22050,8", "1,256,96,128,128,22050,8",
        "1,72,96,128,128,-1,8", "1,72,96,128,128,22050,8,1",
        "1,72,96,128,128,22050,", "1,72,96,128,128,22050,1oops",
        "1,99999999999999999999999999,96,128,128,22050,8"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        assert(!storage_parse_preset(mutable_line(bad[i]), &slot, &voice));
    phrase_lock_t lock;
    assert(storage_parse_lock(mutable_line("3,15,72,96,128,128,22050,3"), &slot, &lock));
    assert(slot == 2 && lock.flags == 7 && lock.voice.bit_depth == 3);
    assert(!storage_parse_lock(mutable_line("3,8,72,96,128,128,22050,3"), &slot, &lock));
    const char *keys[] = {"*", "0", "#"};
    for (unsigned i = 0; i < 3; ++i) {
        char row[100];
        snprintf(row, sizeof row, "%s,7,72,96,128,128,22050,8", keys[i]);
        assert(storage_parse_lock(row, &slot, &lock) && slot == 9 + i);
        snprintf(row, sizeof row, "%s,72,96,128,128,22050,8", keys[i]);
        assert(storage_parse_preset(row, &slot, &voice) && slot == 9 + i);
    }
    assert(!storage_parse_lock(mutable_line("# comment,7,72,96,128,128,22050,8"), &slot, &lock));
    unsigned bank, preset;
    assert(storage_parse_startup(mutable_line("12,7"), &bank, &preset));
    assert(bank == 12 && preset == 7);
    assert(storage_parse_startup(mutable_line("2,4,unused"), &bank, &preset));
    assert(bank == 2 && preset == 4);
    assert(storage_parse_startup(mutable_line("6"), &bank, &preset));
    assert(bank == 6 && preset == 0);
    assert(storage_parse_startup(mutable_line("99,-4"), &bank, &preset));
    assert(bank == 1 && preset == 0);
    assert(!storage_parse_startup(mutable_line("bad"), &bank, &preset));
    assert(bank == 1 && preset == 0);
}

static uint32_t find_bytes(const char *text) {
    size_t n = strlen(text);
    for (size_t i = 0; i + n <= sizeof(disk); ++i)
        if (!memcmp(disk + i, text, n)) return (uint32_t)i;
    assert(!"Text not found in FAT image");
    return 0;
}

int main(void) {
    parsers();
    memset(disk, 0xff, sizeof(disk));
    assert(storage_init());
    storage_diagnostics_t diagnostic = storage_get_diagnostics();
    assert(diagnostic.mount_result == FR_OK && diagnostic.flash_available);
    assert(diagnostic.blank_checked && diagnostic.initially_blank && diagnostic.provision_attempted);
    assert(storage_is_ready() && !storage_host_ready());
    static phrase_bank_t bank, before;
    for (unsigned i = 1; i <= 12; ++i) {
        assert(storage_load_bank(i, &bank));
        assert(bank.count == 12);
    }
    assert(disk[510] == 0x55 && disk[511] == 0xaa);
    assert(!storage_load_bank(0, &bank));
    assert(!storage_load_bank(13, &bank));

    sam_voice_t presets[12];
    uint16_t mask;
    assert(storage_load_presets(presets, &mask) && mask == 0xfff);
    assert(presets[4].bit_depth == 1); /* Old slot 6 is printed key 5. */
    presets[0].bit_depth = 2;
    presets[0].pitch = 117;
    assert(storage_save_presets(presets, mask));
    assert(storage_load_presets(presets, &mask));
    assert(presets[0].bit_depth == 2 && presets[0].pitch == 117);
    phrase_lock_t locks[12];
    assert(storage_load_locks(3, locks) && locks[0].flags == 0); /* No factory locks. */
    memset(locks, 0, sizeof(locks));
    locks[4].flags = LOCK_SAMPLE | LOCK_PITCH;
    locks[4].voice = presets[0];
    assert(storage_save_locks(7, locks));
    assert(storage_load_locks(7, locks));
    assert(locks[4].flags == 5 && locks[4].voice.bit_depth == 2 && !locks[0].flags);
    assert(storage_load_locks(1, locks) && !locks[0].flags && !locks[4].flags);
    for (unsigned i = 9; i < 12; ++i) {
        locks[i].flags = LOCK_SAMPLE;
        locks[i].voice = presets[0];
    }
    assert(storage_save_locks(8, locks));
    assert(storage_load_locks(8, locks));
    for (unsigned i = 9; i < 12; ++i)
        assert(locks[i].flags == LOCK_SAMPLE && locks[i].voice.pitch == 117);
    assert(storage_save_startup(7, 1));
    unsigned start_bank, start_preset;
    assert(storage_load_startup(&start_bank, &start_preset));
    assert(start_bank == 7 && start_preset == 1);
    unsigned previous_writes = writes;
    assert(storage_save_startup(7, 1) && previous_writes == writes);

    assert(storage_load_bank(1, &before));
    uint32_t host_offset = find_bytes("S:Rock on!");
    assert(storage_begin_edit());
    assert(!storage_is_ready() && storage_edit_active() && storage_host_ready());
    assert(!storage_load_bank(1, &bank));
    assert(!storage_save_startup(1, 0));
    assert(!storage_load_presets(presets, &mask));
    assert(!storage_save_presets(presets, mask));
    assert(!storage_load_locks(1, locks));
    assert(!storage_save_locks(1, locks));
    assert(flash_disk_write(host_offset, "S:TEST OK!", strlen("S:TEST OK!")));
    assert(storage_host_eject());
    assert(!storage_host_ready() && storage_edit_active());
    assert(!storage_load_bank(1, &bank));
    storage_service_remount();
    assert(!storage_edit_active() && storage_is_ready());
    assert(storage_reload_pending() && !storage_reload_pending());
    assert(storage_load_bank(1, &bank) && !strcmp(bank.item[9].text, "TEST OK!"));
    assert(storage_init()); /* Reboot must preserve host edits and stored settings. */
    diagnostic = storage_get_diagnostics();
    assert(diagnostic.mount_result == FR_OK && !diagnostic.blank_checked && !diagnostic.provision_attempted);
    assert(storage_load_bank(1, &bank) && !strcmp(bank.item[9].text, "TEST OK!"));
    assert(storage_load_startup(&start_bank, &start_preset) && start_bank == 7 && start_preset == 1);

    fixture("phrases_2.txt", " # comment\r\n p: AY4 AEM \r\nPlain text\nS: \n");
    assert(storage_load_bank(2, &bank) && bank.count == 2);
    assert(bank.item[0].kind == PHRASE_PHONETIC && !strcmp(bank.item[0].text, "AY4 AEM"));
    assert(!strcmp(bank.item[1].text, "Plain text"));
    before = bank;
    char oversize[PVS_MAX_PHRASE + 100];
    memset(oversize, 'A', sizeof(oversize) - 1);
    oversize[sizeof(oversize) - 1] = 0;
    fixture("phrases_2.txt", oversize);
    assert(!storage_load_bank(2, &bank) && !memcmp(&bank, &before, sizeof(bank)));
    fixture("presets.txt", "# old six-column files remain supported\n1,72,96,128,128,22050\n"
            "2,72,96,128,128,22050,4\n2,72,97,128,128,22050,3\n"
            "1,9999,96,128,128,22050,8\nBAD\n");
    assert(storage_load_presets(presets, &mask) && mask == 3);
    assert(presets[0].bit_depth == 8 && presets[1].pitch == 97 && presets[1].bit_depth == 3);
    assert(f_rename("presets.txt", "presets.txt.bak") == FR_OK);
    assert(storage_load_presets(presets, &mask) && mask == 3);
    assert(storage_save_presets(presets, mask));
    FILINFO info;
    assert(f_stat("presets.txt", &info) == FR_OK);
    assert(f_stat("presets.txt.bak", &info) == FR_NO_FILE);

    /* Damage is preserved for host repair, never automatically reformatted. */
    memset(disk, 0xa5, sizeof(disk));
    previous_writes = writes;
    assert(!storage_init());
    diagnostic = storage_get_diagnostics();
    assert(diagnostic.mount_result == FR_NO_FILESYSTEM && diagnostic.blank_checked);
    assert(!diagnostic.initially_blank && !diagnostic.provision_attempted);
    assert(writes == previous_writes);
    assert(storage_load_bank(1, &bank) && !strcmp(bank.item[9].text, "Rock on!"));
    assert(!storage_save_startup(1, 0));
    assert(storage_begin_edit() && storage_host_ready());
    previous_writes = writes;
    assert(!storage_format_factory() && writes == previous_writes);
    assert(storage_host_eject());
    storage_service_remount();
    assert(!storage_is_ready());
    assert(storage_format_factory());
    assert(storage_is_ready() && storage_get_diagnostics().mount_result == FR_OK);
    assert(storage_reload_pending());
    for (unsigned i = 1; i <= 12; ++i)
        assert(storage_load_bank(i, &bank) && bank.count == 12);
    assert(storage_load_startup(&start_bank, &start_preset) && start_bank == 3 && start_preset == 1);
    assert(storage_load_presets(presets, &mask) && mask == 0xfff);
    assert(presets[0].pitch == 96 && presets[4].bit_depth == 1);
    assert(storage_init() && storage_is_ready());
    puts("Storage parser, FAT persistence, host ownership and recovery tests passed.");
    return 0;
}

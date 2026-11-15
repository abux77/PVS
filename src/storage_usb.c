#include "storage.h"
#include "pvs.h"
#include "oled.h"
#include "midi.h"
#include "flash_disk.h"
#include "tusb.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool edit_request;
static bool format_request;
static void (*status_callback)(void);

static void console_bank(void) {
    pvs_bank_state_t state;
    pvs_bank_read(&state);
    char line[64];
    int length = snprintf(line, sizeof line, "PVS BANK 1 %u %lu %d\n",
        state.bank, (unsigned long)state.revision, state.playing);
    if (length > 0 && tud_cdc_connected() && tud_cdc_write_available() >= (unsigned)length)
        tud_cdc_write(line, (unsigned)length);
}

static void console_phrase(unsigned key, unsigned chunk) {
    pvs_phrase_chunk_t phrase;
    if (!pvs_phrase_chunk(key, chunk, &phrase)) return;
    char line[160];
    int length = snprintf(line, sizeof line, "PVS PHRASE 1 %u %lu %u %u %u %c %u ",
        phrase.bank, (unsigned long)phrase.revision, key, chunk,
        phrase.length, phrase.kind, phrase.locks);
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < phrase.count; ++i) {
        line[length++] = hex[phrase.text[i] >> 4];
        line[length++] = hex[phrase.text[i] & 15];
    }
    if (!phrase.count) line[length++] = '-';
    line[length++] = '\n';
    if (tud_cdc_connected() && tud_cdc_write_available() >= (unsigned)length)
        tud_cdc_write(line, (unsigned)length);
}

static void console_controls(const char *result) {
    pvs_controls_t state;
    pvs_controls_read(&state);
    char line[160];
    int length = snprintf(line, sizeof line,
        "PVS PARAMS 1 %u %u %u %u %lu %u %u %u %u %u %u %s\n",
        state.voice.speed, state.voice.pitch, state.voice.mouth, state.voice.throat,
        (unsigned long)state.voice.sample_rate, state.voice.bit_depth,
        state.hardware_mask, state.lock_mask, (unsigned)state.busy,
        (unsigned)state.editable, state.mode, result);
    if (length > 0 && length < (int)sizeof line && tud_cdc_connected() &&
        tud_cdc_write_available() >= (unsigned)length)
        tud_cdc_write(line, (unsigned)length);
}

/* Request/response snapshots: one bounded 64-byte chunk per command. No
 * streaming task, blocking waits or writes from the audio IRQ/core 1. */
static void console_oled(unsigned chunk) {
    static uint8_t snapshot[1024];
    static bool inverted, online, valid;
    static unsigned sequence;
    char line[176];
    if (chunk >= 16 || (chunk && !valid)) return;
    if (!chunk) {
        online = oled_copy_display(snapshot, &inverted);
        valid = true;
        ++sequence;
    }
    int length = snprintf(line, sizeof line, "PVS OLED 1 %u %u %u %u ",
                          sequence, chunk, (unsigned)inverted, (unsigned)online);
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 64; ++i) {
        uint8_t value = snapshot[chunk * 64 + i];
        line[length++] = hex[value >> 4];
        line[length++] = hex[value & 15];
    }
    line[length++] = '\n';
    /* Drop under backpressure; the client detects its response timeout. */
    if (tud_cdc_connected() && tud_cdc_write_available() >= (unsigned)length)
        tud_cdc_write(line, (unsigned)length);
}

void storage_set_status_callback(void (*callback)(void)) {
    status_callback = callback;
}

bool storage_edit_requested(void) {
    bool requested = edit_request;
    edit_request = false;
    return requested;
}

bool storage_format_requested(void) {
    bool requested = format_request;
    format_request = false;
    return requested;
}

static void console_command(char *command) {
    unsigned chunk;
    unsigned key;
    char extra;
    if (!strcmp(command, "bank")) {
        console_bank();
    } else if (sscanf(command, "phrase %2u %1u%c", &key, &chunk, &extra) == 2) {
        console_phrase(key, chunk);
    } else if (sscanf(command, "key %2u%c", &key, &extra) == 1) {
        console_controls(pvs_trigger_key(key));
    } else if (!strcmp(command, "params")) {
        console_controls("ok");
    } else if (!strncmp(command, "set ", 4)) {
        char parameter[12];
        unsigned long value;
        const char *result = "syntax";
        if (sscanf(command, "set %11s %9lu%c", parameter, &value, &extra) == 2 && value <= UINT32_MAX)
            result = pvs_controls_set(parameter, (uint32_t)value);
        console_controls(result);
    } else if (sscanf(command, "oled %2u%c", &chunk, &extra) == 1) {
        console_oled(chunk);
    } else if (!strcmp(command, "bootsel")) {
        pvs_request_bootsel();
    } else if (!strcmp(command, "edit")) {
        if (format_request) printf("Wait for requested format to finish before edit.\n");
        else if (storage_edit_active()) printf("USB edit active: eject the PVS disk to resume.\n");
        else {
            edit_request = true;
            printf("USB edit requested; waiting for audio to stop.\n");
        }
    } else if (!strcmp(command, "format")) {
        if (storage_edit_active() || edit_request)
            printf("Format refused: eject USB edit disk or restart before formatting.\n");
        else {
            format_request = true;
            printf("Factory format requested; waiting for audio to stop.\n");
        }
    } else if (!strcmp(command, "status")) {
        storage_diagnostics_t diagnostic = storage_get_diagnostics();
        printf("Storage: %s; medium %s\n", storage_edit_active() ? "USB host owns disk" :
               storage_is_ready() ? "firmware owns disk" : "factory fallback",
               storage_host_ready() ? "available to host" : "not available to host");
        printf("Storage diagnostics: last FAT result %u (0=OK,1=I/O error,3=not ready,13=no FAT); flash:%s\n",
               diagnostic.mount_result, diagnostic.flash_available ? "available" : "unavailable");
        printf("At boot: reserved region %s; factory provisioning %s\n",
               !diagnostic.flash_available ? "unavailable" :
               !diagnostic.blank_checked ? "already mountable" :
               diagnostic.initially_blank ? "erased" : "nonblank (preserved)",
               diagnostic.provision_attempted ? "attempted" : "not attempted");
        if (status_callback) status_callback();
    } else if (*command) {
        printf("Commands: bank; phrase KEY CHUNK; key 0..11; params; set speed|pitch|mouth|throat|rate|bits VALUE; oled 0..15; bootsel; status; edit; format (ERASE PVS files).\n");
    }
}

void storage_console_service(void) {
    static char command[48];
    static unsigned length;
    static bool overflow;
    /* Called on core 0 only. The application disables stdio's background
     * TinyUSB task so no ISR can re-enter a FatFs/flash transaction. */
    tud_task();
    midi_task();
    storage_service_remount();
    unsigned budget = 64;
    while (budget-- && tud_cdc_available()) {
        unsigned char c;
        if (tud_cdc_read(&c, 1) != 1) break;
        if (c == '\r' || c == '\n') {
            if (!overflow) {
                while (length && isspace((unsigned char)command[length - 1])) --length;
                command[length] = '\0';
                console_command(command);
            }
            length = 0;
            overflow = false;
        } else if ((c == '\b' || c == 127) && length) {
            --length;
        } else if (c >= 32 && c < 127) {
            if (!length && c == ' ') continue;
            if (length + 1 < sizeof(command)) command[length++] = (char)tolower(c);
            else overflow = true;
        }
    }
    tud_cdc_write_flush();
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor[8], uint8_t product[16], uint8_t revision[4]) {
    (void)lun;
    memcpy(vendor, "PVS     ", 8);
    memcpy(product, "Editable Files  ", 16);
    memcpy(revision, "0200", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    if (lun == 0 && storage_host_ready()) return true;
    tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0);
    return false;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *blocks, uint16_t *block_size) {
    (void)lun;
    *blocks = PVS_DISK_SECTOR_COUNT;
    *block_size = PVS_DISK_SECTOR_SIZE;
}

bool tud_msc_is_writable_cb(uint8_t lun) {
    return lun == 0 && storage_host_ready();
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
    (void)power_condition;
    if (lun != 0) return false;
    if (load_eject && !start) return storage_host_eject();
    return storage_host_ready();
}

static bool transfer_valid(uint8_t lun, uint32_t lba, uint32_t offset, uint32_t size,
                           uint32_t *absolute) {
    uint64_t address = (uint64_t)lba * PVS_DISK_SECTOR_SIZE + offset;
    if (!tud_msc_test_unit_ready_cb(lun)) return false;
    if (address > PVS_STORAGE_BYTES || size > PVS_STORAGE_BYTES - address) {
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0);
        return false;
    }
    *absolute = (uint32_t)address;
    return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t size) {
    uint32_t address;
    if (!transfer_valid(lun, lba, offset, size, &address)) return -1;
    if (!flash_disk_read(address, buffer, size)) {
        tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x11, 0);
        return -1;
    }
    return (int32_t)size;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size) {
    uint32_t address;
    if (!transfer_valid(lun, lba, offset, size, &address)) return -1;
    /* Persist before acknowledging the write, not after TinyUSB has sent CSW.
     * A 4 KiB MSC buffer coalesces host transfers at flash erase granularity. */
    if (!flash_disk_write(address, buffer, size) || !flash_disk_sync()) {
        tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x0c, 0);
        return -1;
    }
    return (int32_t)size;
}

int32_t tud_msc_scsi_cb(uint8_t lun, const uint8_t command[16], void *buffer, uint16_t size) {
    (void)buffer;
    (void)size;
    if (command[0] == 0x35) { /* SCSI SYNCHRONIZE CACHE (10) */
        if (tud_msc_test_unit_ready_cb(lun) && flash_disk_sync()) return 0;
        tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x0c, 0);
        return -1;
    }
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0);
    return -1;
}

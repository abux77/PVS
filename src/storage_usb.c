#include "storage.h"
#include "pvs.h"
#include "flash_disk.h"
#include "tusb.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool edit_request;
static bool format_request;
static void (*status_callback)(void);

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
    if (!strcmp(command, "bootsel")) {
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
        printf("Commands: bootsel (firmware update); status; edit (eject to resume); format (ERASE PVS files, restore factory defaults).\n");
    }
}

void storage_console_service(void) {
    static char command[48];
    static unsigned length;
    static bool overflow;
    /* Called on core 0 only. The application disables stdio's background
     * TinyUSB task so no ISR can re-enter a FatFs/flash transaction. */
    tud_task();
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

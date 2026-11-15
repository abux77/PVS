#include "midi.h"
#include "pvs.h"
#include "tusb.h"

void midi_handle_packet(const uint8_t packet[4]) {
    /* Reject other cables/channels, invalid data bytes and mismatched CINs. */
    if ((packet[0] >> 4) || (packet[1] & 15) || packet[2] > 127 || packet[3] > 127) return;
    if (packet[0] == 0x0b && packet[1] == 0xb0)
        pvs_midi_cc(packet[2], packet[3]);
    else if (packet[0] == 0x09 && packet[1] == 0x90 && packet[3])
        pvs_midi_note(packet[2], packet[3]);
}

void midi_task(void) {
    uint8_t packet[4];
    /* Bounded per service call, including during rendering/playback. */
    unsigned budget = 16;
    while (budget-- && tud_midi_packet_read(packet)) midi_handle_packet(packet);
}

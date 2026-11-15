#pragma once
#include <stdint.h>
void midi_task(void);
/* One USB-MIDI 1.0 event packet; cable 0, channel 1 only. */
void midi_handle_packet(const uint8_t packet[4]);

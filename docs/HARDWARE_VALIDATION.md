# Hardware acceptance checks

Compilation and host tests cannot validate audio continuity, electrical matrix
behavior, USB enumeration, or flash timing on the instrument. Record the board,
firmware build, and result when running these checks. They require a USB cable;
a debug probe is not required.

## Flash and serial

1. Copy the board's `pvs.uf2` to its BOOTSEL drive. Use the RP2040 build for Pico
   and the RP2350 build for Pico 2.
2. Open the USB serial port and request `status`. Check the board, clock, pins,
   storage state, bank, all four voice values, sample rate, and bit depth.
3. On factory data, startup state is bank 3, preset 1: speed 72, pitch 96, mouth
   128, throat 128, rate 22050 Hz, depth 8. A previously edited volume should
   retain its own startup state across a firmware update.

## Speech and controls

The default audio backend is hardware PWM/DMA. Verify `status` reports
`PWM slice0` and a DMA timer, then repeat the previously crackling phrase with
unchanged settings. Test live sample rate, all bit depths, short words, long
phrases and cancellation. Check midpoint idle and GP0 carrier through the
existing low-pass filter. Audio remains on core 0 for this comparison.

The optional PIO backend (`PVS_AUDIO_PIO=ON`) retains `Audio FIFO underruns`.
Hardware PWM has no TX FIFO, so that diagnostic does not apply. `restarts`
counts software buffer resumes and is not a measured audio-gap count.
DMA completion handoff now has one sample period of timing budget instead of
PIO FIFO buffering; hardware testing must confirm continuity at 88200 Hz.

1. Speak both `S:` and `P:` phrases. The selected sound source is the original
   upstream C renderer; compare its PCM fixtures/output to recordings with the
   same voice and circuit. Python remains the UI/parameter behavior reference.
2. Verify speed and pitch independently. After a mode change or preset recall,
   each pot must wait for movement equivalent to 800 counts on a 16-bit ADC.
3. Tap B: LED pair 1/2 blue; pot 1 controls 88200–8000 Hz (reversed direction) with 22050 near the
   midpoint and 50 Hz steps; pot 2 controls the reference's eight bit-depth
   bands. Change rate during a long word and check continuity. Exit A and
   confirm the selected rate and bit depth persist.
4. Tap A: LED pair 7/8 white; pot 1 controls mouth, pot 2 throat. Select B,
   B, C and D in turn: each replaces the previous mode and only its LED
   pair lights. Tap the active mode again to return to Normal. Specifically
   check C to A restores rate/bit-depth controls with fresh pot pickup.
5. Speak a phrase, tap C (LED pair 5/6 red), and play keys 1–9, *, 0, #. All notes must
   replay that last phrase from C through B. Repeat notes out of order to check
   that pitch derives from the base value without accumulating error.
6. Move pots between words in a long phrase. Voice changes apply at word
   boundaries, while sample-rate changes apply during playback.

## Matrix and LEDs

1. Verify all physical buttons against this reference layout:

   ```text
    1   2   3   A
    4   5   6   B
    7   8   9   C
    *   0   #   D
   ```

2. Check rapid taps, held keys, same-row modifier combinations and releases
   during speech. Each action should produce one debounced event. Inactive
   columns must remain high impedance.
3. For Pico 2, reproduce the reference hardware's external 10 kΩ pull-ups from
   GP10–GP13 to 3V3. Verify the electrical behavior on the actual board revision.
4. Verify LED pair 3/4 green in preset mode and exclusive mode indicators.

## Banks, presets and locks

1. Hold D with each keys 1–9, *, 0, # to load the corresponding bank; releasing D must
   not enter preset mode. Power cycle and check bank persistence.
2. Tap D, short press a phrase button: recall its preset, hear “Preset loaded,”
   leave preset mode and wait for pot movement. Power cycle to verify recall.
3. Tap D, hold a phrase button for at least two seconds: store its preset,
   hear “Preset stored,” and leave preset mode. Recall and inspect all six
   stored values.
4. Hold B/C/A with a phrase button, then release that phrase button to
   store sample/pitch/voice locks respectively. Hold the phrase for two seconds
   instead to clear only that group. Pitch locks include speed.
5. Combine all three lock groups on a phrase. Pots must not override its locked
   parameters. An unlocked phrase must still use the global voice. Verify
   persistence, per-bank isolation and chromatic playback of a locked phrase.
6. Hold D and press B to clear every lock in the current bank immediately.

## USB editing and persistence

1. Send `edit` over serial while idle. Confirm the `PVS` disk becomes available
   and synth buttons are suspended while the host owns it.
2. Edit a phrase, a preset and startup state using a plain-text editor. Eject
   the disk using the operating system; wait for serial remount/reload output.
   Speak the changed phrase and power cycle to verify persistence.
3. Try CRLF files, comments, empty files and malformed/out-of-range records.
   Check diagnostics and ensure malformed input does not crash or corrupt RAM.
4. Enter edit mode during speech: the request must wait until audio has stopped.
   Firmware must never access mounted files while the host has ownership.
5. Unplug/reconnect while in edit mode and exercise normal host eject. Keep a
   backup before deliberately interrupting a host write: FAT is not journaled,
   and unplugging during a write can corrupt the volume.
6. After updating firmware, confirm existing user files remain intact. Factory
   defaults must only be installed on erased storage, never over a damaged or
   unrecognized existing volume.

## Sustained operation

Run long phonetic and text phrases at both rate extremes and slow speech
settings while moving pots and tapping buttons. Check audio for gaps and USB
for disconnects. Repeat on each MCU before describing the port as hardware
validated. A successful Pico 2 build alone does not establish parity.

## Cache, retrigger and button-driven USB edit

1. Play the same phrase twice unchanged. Check cache hits increase on the
   second play, with the same sound and timing. Change speed/pitch/mouth/throat
   and confirm affected words render again. Check sample rate and bit depth
   still apply to cached audio, including live rate changes.
2. Repress the same phrase button during a word and during a punctuation pause.
   It must restart once, immediately after stopping the old playback. Repeat
   during first-time rendering and cached playback. Other buttons stay queued.
3. Try a phrase too long for the reservation: speech must complete by streaming.
4. Press A+D in either order, idle and during speech. Release both, edit,
   save and eject. Confirm speech stops before mounting and neither mode toggles.

## OLED

Power-cycle and check that the complete PVS logo stays visible for three seconds,
then changes to the current mode. Check orientation with the new PCB mounting (rotated 180 degrees from the prototype)
and confirm buttons/audio still respond during the splash.

1. With SDA GP15, SCL GP14 and 3.3 V power, reboot and check `status` reports
   OLED online at 0x3C or 0x3D. Check readable orientation and full 128x64 area.
   Confirm the large font and three-line mode/pot layout fit, especially
   CHROMATIC, 88200 HZ and THROAT 255.
2. Select every mode, bank and preset; check the heading and settings. Play
   text and phonetic phrases, move pots, retrigger and verify the screen keeps
   up without audio crackles. The OLED now shows controls only; phrase text remains in serial diagnostics.
3. Enter A+D USB edit and verify the save/eject message; eject and verify the
   normal UI returns. Check that speech remains correct with OLED unplugged
   at boot and that a bus fault disables display updates without hanging audio.

4. Without playing a phrase, move both knobs in Normal, Sample, Voice and
   Chromatic modes. Check the display and next spoken phrase use the new
   values after pickup; changing modes must not cause a value jump.

5. Check USB EDIT / SAVE FILES / EJECT FILES stay in that order. Bank loads,
   lock saves and preset loads/stores should show their confirmations for
   1.5 seconds for loaded messages and three seconds for stored/locked messages, then return to the mode/pot screen without pausing audio.

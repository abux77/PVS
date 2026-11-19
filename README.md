# Portable Voice Synthesizer — native Pico SDK firmware

PVS runs the supplied MicroPython instrument's SAM synthesis and controls in C
on Raspberry Pi Pico (RP2040), with a Pico 2 (RP2350) build from the same source
tree. The original Python reference has been removed; regression fixtures preserve
its tested behavior. Physical acceptance
testing is described in [docs/HARDWARE_VALIDATION.md](docs/HARDWARE_VALIDATION.md);
a successful build or host test does not establish hardware parity.

## Online resources

- **[PVS Web App](https://abux77.github.io/PVS/)** — browser-based PVS companion app.
- **[Interactive PVS User Guide](https://abux77.github.io/PVS/pvs-user-guide/)** — illustrated v0.4 documentation with clickable keypad controls.

## Build and flash

The project uses Pico SDK 2.3.1, CMake, Ninja, an ARM toolchain, and Python 3 for
embedding factory text files. The VS Code Pico extension's installed SDK and
toolchain are supported. SAM and FatFs sources are local; firmware configuration
does not fetch an unpinned SAM dependency.

```sh
cmake -S . -B build -G Ninja -DPICO_BOARD=pico
cmake --build build
```

If the SDK is not discovered, set `PICO_SDK_PATH` to your SDK installation. For
Pico 2, use a separate build directory so board-specific caches cannot mix:

```sh
cmake -S . -B build-pico2 -G Ninja -DPICO_BOARD=pico2
cmake --build build-pico2
```

Each build produces `pvs.elf`, `pvs.bin`, `pvs.hex`, and `pvs.uf2`. Hold BOOTSEL
while connecting the correct board, then copy its UF2 to the bootloader drive.
Normal diagnostics use USB serial; no SWD probe is required.

Run the host checks with a C compiler supporting AddressSanitizer/UBSan:

```sh
python3 tests/run_tests.py
```

They exercise the real controls and FAT backend, every factory phrase through
the speech pipeline, malformed-input handling, cancellation, the Python
reciter/parser and bit-depth mappings, and PCM fixtures from the original C
renderer. Hardware I/O is mocked in these tests.

## Controls

The keypad uses the printed labels shown below:

```text
 1   2   3   A
 4   5   6   B
 7   8   9   C
 *   0   #   D
```

| Action | Behavior |
| --- | --- |
| keys 1–9, *, 0, # | Speak the assigned phrase |
| Tap B | Toggle sample mode: pot 1 rate (direction reversed), pot 2 bit depth |
| Press A + D together | Stop speech and mount the USB editing drive; eject to resume |
| Press a phrase key during playback | Stop the current phrase and start the selected phrase from the beginning |
| Hold D + keys 1–9, *, 0, # | Load phrase bank 1–12 |
| Tap D | Enter or cancel preset mode |
| Preset mode: short keys 1–9, *, 0, # | Recall preset, speak confirmation, exit |
| Preset mode: hold keys 1–9, *, 0, # ≥2 s | Store preset, speak confirmation, exit |
| Tap C | Toggle chromatic mode: keys 1–9, *, 0, # replay the last phrase at C–B |
| Tap A | Toggle voice mode: pot 1 mouth, pot 2 throat |
| Hold B/C/A + phrase, release phrase | Store sample/pitch/voice lock |
| Hold that phrase ≥2 s | Clear that lock group |
| Hold D, press B | Immediately clear all locks in this bank |

Sample, Preset, Chromatic and Voice modes are mutually exclusive. Tapping a
different mode button leaves the previous mode; tapping the active mode returns
to Normal. Bank, lock and USB-edit chords keep their existing behavior. Each pot waits
for 800 counts of movement on the reference's 16-bit scale after its context
changes or a preset is recalled. The selected sample rate persists when sample
mode is turned off. Voice modulation happens before each word; rate modulation
also runs during playback. Locked groups override the relevant live controls
for their phrase and leave the global voice intact.

LED pairs are sample blue (1–2), preset green (3–4), pitch red (5–6), and voice
white (7–8). Default voice is speed 72, pitch 96, mouth 128, throat 128, 22050 Hz,
8 bits. The supplied factory startup file selects bank 3 and preset 1.

## Editable files over USB

The last 1 MiB of the board's QSPI flash is reserved for a FAT volume. A linker
assertion prevents firmware from overlapping it. On first boot, completely
erased storage is formatted and seeded from `data/`. Later firmware updates
preserve that volume. An existing damaged or unrecognized volume is never
automatically formatted.

Open the USB serial port and send `edit` followed by Enter. After speech stops,
firmware unmounts the filesystem and the host can mount the `PVS` USB disk. Edit
files using a plain-text editor, then **eject the disk in the operating system**.
Firmware remounts and reloads files only after host eject. USB serial remains
available. Synth controls are suspended while the host owns the disk.

If a previously used Pico reports `factory fallback` and FAT result 13, its
reserved flash may contain an older filesystem. To discard those files and
install PVS factory files, send `format` while **not** in USB edit mode. Wait for
`PVS format complete (FAT result 0)`, then send `edit`. Formatting affects only
the reserved PVS volume and destroys its previous files.

- `phrases_1.txt` … `phrases_12.txt`: `S:HELLO THERE` for text,
  `P:AY4 AEM AH ROH4BOWLAORD` for phonemes; `#` lines are comments.
- `presets.txt`: `preset,speed,pitch,mouth,throat,sample_rate,bit_depth`.
- `locks_1.txt` … `locks_12.txt`:
  `phrase,flags,speed,pitch,mouth,throat,sample_rate,bit_depth`.
  Flags are pitch/speed `1`, voice `2`, sample `4`; combine them by addition.
- `startup_state.txt`: `bank,preset`; preset `0` means no startup recall.

Missing lock files mean no locks. Numeric input and phrase lengths are checked;
the firmware uses fixed buffers and reports rejected records. Factory phrases
are a fallback when storage is unavailable. FAT is not journaled: keep a backup
and eject before disconnecting, especially after an edit. Firmware and host
never mount the same filesystem for writing at the same time.

## Implementation

- `third_party/sam/`: the existing upstream C renderer, copied from the original
  dependency with provenance and bounded output adaptations. It is the sound
  source selected for this port.
- `src/sam/`: bounded translations of the Python reciter/phoneme rules plus
  the C renderer adapter. Parser tables/rules are checked in and maintained directly; PCM is streamed without an utterance-sized
  allocation.
- `src/audio_pwm.c`: hardware PWM on GP0 (slice 0), with a fractional DMA
  timer pacing 8-bit samples through two 2048-sample halfword buffers (8 KiB).
  The carrier is system clock / 256 (976.5625 kHz at 250 MHz); idle is 50% duty.
  DMA IRQ0 hands off buffers at highest priority; no per-sample CPU interrupt.
  Audio reserves both compare channels of slice 0, two DMA channels and one
  DMA timer. Live rate and bit-depth controls remain available on core 0.
- `src/audio_pio.c`: previous PIO0 SM0 backend, retained for comparison. Select
  it with `cmake -S . -B build -DPVS_AUDIO_PIO=ON`, then rebuild. Restore the
  hardware PWM default with `-DPVS_AUDIO_PIO=OFF`.
- `src/matrix.c`: autonomous PIO scan with high-impedance inactive columns,
  DMA history, debounce and buffered events.
- `src/pots.c`: ADC/DMA sampling of GP26/GP27 with averaging.
- `src/ws2812.c`: PIO LED output on GP22.
- `src/pvs.c`: mode state, pickup, presets, locks, remembered phrase and word
  modulation.
- `src/storage*.c`, `src/flash_disk.c`, `src/usb_descriptors.c`: bounded file
  parsing, reserved-flash block device, FatFs and exclusive CDC/MSC edit mode.

RP2040 uses the reference's 250 MHz clock; RP2350 uses its 150 MHz clock. Pico 2
matrix wiring follows the reference's external 10 kΩ row pull-ups. PIO/DMA
allocation and actual voice/storage state are available through USB diagnostics.

The renderer is the existing C implementation, as requested. Its prosody and
formant transitions differ from the Python fallback renderer. Application
controls, reciter exceptions and phoneme processing follow the supplied Python
reference. The test fixtures include PCM comparisons against the original C
renderer; physical audio and USB validation are still required.

### RAM diagnostics and sample reservation

At boot, after subsystem initialization, firmware reserves up to 128 KiB of
contiguous heap for unsigned 8-bit sample caching, leaving 32 KiB plus
allocator overhead headroom. If that allocation fails it tries smaller sizes;
a zero reservation does not prevent streamed speech. The reservation is held
for the lifetime of the firmware and exposed through `pvs_sample_buffer()`.

`status` reports remaining heap (unclaimed space plus reusable allocator
blocks), the boot heap space, and the actual reserved sample capacity. Heap
figures exclude the linker-reserved stacks and are not stack high-water marks;
allocator free blocks are not necessarily one contiguous allocation.

The sample reservation now holds a last-phrase raw PCM cache. Completed word
segments (up to 96) are reused when the phrase/input type and that word's
speed, pitch, mouth and throat match. Word callbacks still run, preserving
locks, chromatic pitch and word modulation. Sample rate and bit depth are
applied during playback, so neither requires regenerating cached raw PCM.
`status` reports cached bytes, completed segments, hits and renderer calls.

First playback streams while capturing. Incomplete/cancelled segments are
never reused. If a segment exceeds the available space, it streams normally;
earlier complete segments can still be reused. No extra large cache is
allocated, and the two 2048-sample DMA buffers remain in use. A 128 KiB reserve
holds approximately 5.94 seconds at 22050 Hz in unsigned 8-bit PCM.

Pressing the same unmodified phrase button during speech stops the current
output and starts the phrase again. Completed cached words are reused;
unfinished words are regenerated. Pressing a different unmodified phrase key
stops playback and starts that phrase; an uncached phrase must first render.
Mode and modifier events retain their queued behavior. Press A+D together to request USB editing without a serial
monitor. Speech stops before the host gains disk access. Release both buttons,
edit/save files, and eject the PVS disk to resume. The chord does not toggle
Sample or Voice mode. Pot 1's sample-rate mapping now runs from 88200 Hz at ADC
minimum to 8000 Hz at ADC maximum; normal speech-speed mapping is unchanged.

### Printed telephone-style keypad

Phrase-file order is now `1, 2, 3, 4, 5, 6, 7, 8, 9, *, 0, #`.
Lock and preset CSV rows use those printed key labels, and serial playback
messages use the label directly (`7: ...`). A/B/C/D are
Voice/Sample/Chromatic/Preset-bank; A+D mounts the editing drive.

Existing disk files need a one-time conversion, not just new comments.
See [the editing and migration guide](docs/KEYPAD_LAYOUT.txt). Factory files
in `data/` are already converted. Bank filenames remain numbered 1–12;
keys *, 0 and # select banks 10, 11 and 12 respectively.

`status` and startup print a firmware build number and UTC build time. The
number increments on each build invocation within that build directory; it
restarts if that directory is deleted. Board builds have separate counters,
so use board, number and timestamp together when reporting a problem.

### SSD1306 OLED (128x64, four-pin I2C module)

At boot, the supplied PVS logo appears for three seconds before the current
mode and pot values. Controls and audio remain responsive during the splash.
See the [monochrome boot preview](docs/OLED_BOOT_PREVIEW.png). The bitmap is
compiled into flash; regenerate it from `assets/pvs-logo.png` with
`python3 tools/generate_oled_logo.py` (requires Pillow).

Connect SDA to **GP15**, SCL to **GP14**, VCC to **3V3 OUT**, and GND to GND.
The driver owns PIO0 SM1 (7 instructions, no DMA/IRQ) for open-drain I2C up to 400 kHz and probes addresses 0x3C then 0x3D at boot.
Use module pull-ups to 3.3 V (the firmware also enables GPIO weak pull-ups).

The display uses the Audiowide font and three large lines:
mode, pot 2 parameter/value, and pot 1 parameter/value (matching the physical pots). Normal and Preset show
Speed/Pitch; Sample shows e.g. 22050 HZ / 8 BIT; Voice shows Mouth/Throat;
Chromatic shows Speed/Pitch. Knobs update while idle at 20 Hz after soft
takeover; during speech, voice changes still apply at word boundaries and
sample-rate changes remain live. Values describe the pot-controlled base settings, not a
phrase lock's temporary override. USB editing has fixed top-to-bottom order: USB EDIT / SAVE FILES / EJECT FILES.
Bank loads show BANK X / LOADED for 1.5 seconds. Preset loads also last 1.5 seconds. Successful lock saves show
PARAMETER / LOCKED and preset saves show PRESET / STORED for three seconds.
All notices use black text on a white background. Normal and USB
edit screens retain white text on black. Background inversion uses the OLED
controller's A7/A6 command rather than redrawing the framebuffer, eliminating
the slow background wipe while using the PIO I2C transport. These messages do not block audio or knob updates; afterward
the normal mode/pot screen returns. USB editing takes priority over notices.
The phrase and bank remain available through serial diagnostics.

The font is baked into `src/oled_font.h`; normal firmware builds need no font
library. To regenerate, run `tools/generate_oled_font.py` with Pillow installed.
The Audiowide TTF and SIL Open Font License are in `assets/fonts/`.
See [screen previews](docs/OLED_PREVIEW.png).

Updates use two fixed 1 KiB screen buffers and send only changed 16-column
chunks. Each chunk uses two bounded I2C writes (2 ms timeout each), with audio
interrupts enabled. There is no full-screen blocking transfer during speech.
A missing or failed display disables display traffic; speech remains available.
`status` reports online/offline, address and I2C errors. Reboot after connecting
or repairing the OLED to retry detection. No new DMA channels or IRQs are used. PIO0 SM1 is reserved for the OLED;
PIO0 SM0 remains available for optional PIO audio. The reversed SDA/SCL PCB
wiring is supported directly, without using a hardware I2C controller.

Protocol reference: [SSD1306 datasheet](https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf).

OLED digits have a fixed advance width. Voice values use three digits (072),
sample rate uses five (08000 HZ), and bit depth stays one digit (8 BIT), so
changing values no longer shifts the centered readouts.

### Firmware updates without BOOTSEL

Send `bootsel` in the USB serial monitor, or press **B+C together**.
The OLED shows FIRMWARE / FLASH / BUILD NNN for 1.5 seconds before rebooting.
Playback stops and firmware storage is synchronized and unmounted before
the Pico enters its USB bootloader. If the PVS editing disk is mounted,
eject it first; the pending update request waits for that eject.
The serial port disconnects and the **RPI-RP2** drive appears on Pico.
Copy `build/pvs.uf2` to that drive. The Pico reboots into the new firmware.
Install this version once using the physical BOOTSEL button to enable these shortcuts.


## Repository contents and licensing

- `src/`, `include/`: firmware and hardware drivers.
- `data/`: editable factory phrase/configuration files embedded for provisioning.
- `third_party/`: vendored SAM renderer and FatFs, including upstream notices.
- `tests/`: host regression tests; `docs/`: hardware checks and user references.
- `assets/`, `tools/`: OLED source artwork/fonts and reproducible generators.

See [third-party notices](THIRD_PARTY_NOTICES.md) before redistributing.
Original PVS code and documentation are [MIT licensed](LICENSE).
The logo and its OLED derivatives use [CC BY-NC 4.0](assets/LOGO_LICENSE.md).
SAM, FatFs, and the font retain their separate terms. Removing the Python
reference does not resolve the licensing of SAM-derived C code or tables.

Build numbers increment within each local build directory; they are diagnostic
identifiers, not globally unique release versions. Publish firmware as GitHub
Release attachments with the board and source commit identified, rather than
committing the build directory. See [contributing](CONTRIBUTING.md).

Speed spans 255 to 1 across increasing ADC readings (lower SAM values speak
faster). Mouth now spans 255 to 1 in the same direction. Sample playback spans
8000–88200 Hz, retaining 22050 Hz near the pot midpoint and 50 Hz steps.
The higher rate changes playback speed/pitch; SAM still generates its native PCM.

## Browser companion

Open the hosted companion at **https://abux77.github.io/PVS/** in desktop
Chrome or Edge, or run it locally as described below.

The [local companion page](web/README.md) mirrors the hardware OLED at an
integer scale of 4×–8×, displays all six voice parameters, and supports web
edits while physical knobs and phrase locks are not controlling them. Serve
`web/` on localhost, open it in desktop Chrome or Edge, and click **Connect
PVS**. This requires companion-enabled firmware. Synthesis stays on the instrument;
web edits are session values, with the existing hardware presets used to save.

## USB MIDI input

PVS exposes a class-compliant **PVS MIDI** USB port alongside its existing
serial console and file-editing drive. Receive channel: **1** (one-based).
No MIDI application or browser is required on the Pico; select its MIDI port
in your DAW or MIDI controller routing software. PVS is a USB device and needs
a USB host between it and another USB-only controller.

| MIDI CC | Parameter | CC 0 → 127 |
| --- | --- | --- |
| 20 | Speed | 1 → 255 |
| 21 | Pitch | 0 → 255 |
| 22 | Mouth | 0 → 255 |
| 23 | Throat | 0 → 255 |
| 24 | Sample rate | 8000 → 88200 Hz |
| 25 | Bit depth | 1 → 8 bits |

Values are scaled linearly and rounded to the nearest integer. Larger Speed
values follow SAM's native parameter (longer/slower speech). Each CC updates
its selected base value and rearms that knob's pickup at its current position;
a stationary knob does not overwrite MIDI. Moving that knob by the existing
pickup threshold restores hardware control. Other parameters keep their
current ownership. MIDI edits are session values and do not save to flash.
Active phrase locks retain playback priority; MIDI edits to a locked group
become audible when the lock is no longer applied. Timbre and depth take effect
at the next word boundary; sample rate can change during playback.

Channel-1 Note On messages map **notes 36–47** to the current bank's keys
**1, 2, 3, 4, 5, 6, 7, 8, 9, *, 0, #**, respectively. This always triggers a
phrase, including while the hardware is in Voice, Sample, Chromatic or Preset
mode. It never recalls/stores a preset or switches bank. Velocity 1–127 triggers
the phrase at the instrument's existing level. Note Off and velocity-zero
Note On are ignored: playback is one-shot. New notes interrupt/retrigger speech
without recursively entering the renderer; the latest note in a burst wins.
Unassigned keys, notes outside 36–47, other channels, unsupported CCs, and
malformed packets are ignored. MIDI is ignored during USB editing or a pending
firmware reboot. USB serial, virtual/physical OLED, and web controls remain
available alongside MIDI; the web readouts follow incoming CC changes.

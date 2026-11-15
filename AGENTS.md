# AGENTS.md --- PVS Portable Voice Synthesizer

## Licensing and removed reference

Original Andrew Buckie code/documentation is MIT licensed (LICENSE).
The logo and derivatives use CC BY-NC 4.0 (assets/LOGO_LICENSE.md).
Third-party exclusions are listed in THIRD_PARTY_NOTICES.md.
The Python reference and its table generator were removed at the owner's request.
Historical Python instructions below are archival: use current C behavior,
README.md and tests/sam_behavior_golden.json instead. The checked-in C tables
are maintained directly; do not attempt to regenerate them from removed files.

## Current validated C behavior

Later user-requested C behavior overrides the historical port notes below.
See README.md for the current UI: A=Voice, B=Sample, C=Chromatic, D=Preset/bank;
A+D=USB edit, B+C=BOOTSEL. PWM/DMA is the default audio backend, with PIO
retained as an optional comparison backend. The SAM renderer is vendored;
the initial dependency linker issues described below have been resolved.
Keep the original Python reference unchanged when making C-only UI changes.

## Project purpose

PVS (Portable Voice Synthesizer) is a standalone Raspberry Pi Pico
speech synthesizer based on SAM (Software Automatic Mouth).

This repository is a Pico SDK C port of the working MicroPython PVS
firmware. The `reference_python/` directory is the behavioral reference.
Preserve that behavior unless a change is explicitly requested.

The goal is a maintainable native C implementation suitable for the
finished hardware instrument, not merely a project that compiles.

## Development environment

-   Framework: Raspberry Pi Pico SDK
-   Current SDK: 2.3.1
-   Build: CMake + Ninja
-   IDE: VS Code with Raspberry Pi Pico extension
-   Development host: macOS / Apple Silicon
-   Primary target: Raspberry Pi Pico / RP2040
-   Future target: Raspberry Pi Pico 2 / RP2350
-   Prefer one source tree supporting both targets.
-   USB stdio should be available for diagnostics.
-   A SWD/CMSIS-DAP Debug Probe must not be required.
-   Normal workflow: edit -\> compile -\> UF2 -\> flash -\> USB serial
    test.

Do not edit generated files under `build/` as a permanent fix. `_deps`
inside `build/` may be regenerated. Patch, vendor, wrap, or configure
dependencies from source-controlled files.

## Current build status

The initial C port compiles its source files on RP2040 but currently
encounters linker issues in the upstream SAM dependency:

1.  Duplicate globals `A`, `X`, and `Y` are defined by upstream `sam.c`
    and `reciter.c`.
2.  There are undefined references to the upstream SAM global `debug`.

Fix these reproducibly in source control. Do not rely on manual edits to
`build/_deps/...`.

The project currently uses an upstream C SAM implementation. The
original PVS also contains an existing native C SAM renderer. Use the
Python reference and original renderer where needed to preserve
PVS-specific behavior.

## Hardware

### MCU

-   Raspberry Pi Pico / RP2040
-   Original MicroPython firmware ran at 250 MHz.
-   Future: Pico 2 / RP2350.
-   Isolate architecture-specific code.

### Audio

-   Audio output: GP0
-   Original implementation: PIO PWM audio
-   Default SAM sample rate: 22050 Hz
-   Live sample-rate range: approximately 8000--44100 Hz
-   Original audio used PIO0 SM0.
-   Original SAM audio DMA was reported as DMA 0.
-   Final implementation should use PIO + DMA.
-   Preserve live sample-rate control and retain the selected rate after
    leaving Sample Rate Mode.
-   Preserve current bit-depth/sample behavior from the Python
    reference.

Avoid large PCM allocations where practical. Prefer a bounded/streaming
pipeline if it preserves SAM output:

    text -> reciter -> phonemes -> SAM renderer -> bounded PCM buffer -> DMA -> PIO PWM -> GP0

### Pots

-   Pot 1: GP26 / ADC0
-   Pot 2: GP27 / ADC1
-   Preserve soft takeover/pickup behavior.
-   Historical pickup threshold: 800.
-   Avoid control jumps on mode changes.

Historical contexts:

Pot 1: - speed - mouth - sample rate

Pot 2: - pitch - throat - chromatic/base pitch - sample-mode controls as
defined by the current reference

The current `reference_python/` implementation is authoritative.

### 4x4 matrix

Columns: - GP6 - GP7 - GP8 - GP9

Rows: - GP10 - GP11 - GP12 - GP13

Original scanner: - PIO1 SM0 (`sm_num=4` in MicroPython) - DMA circular
history buffer - \~15 ms debounce - later ring size \~512 entries

Final C should preferably use autonomous PIO/DMA or another robust
nonblocking method. Do not let matrix scanning disrupt audio.

Buttons are physically numbered 1--16. Be careful about 0-based internal
indexes versus 1-based user-facing numbering.

### WS2812

-   8 WS2812B status/UI LEDs.
-   Use the exact pin and behavior from the current reference source.
-   Prefer PIO.

## SAM voice parameters

Core parameters: - Speed - Pitch - Mouth - Throat

Historical defaults: - Speed 72 - Pitch changed during development; use
current reference - Mouth 128 - Throat 128

Historical pot mappings: - Speed: 255 -\> 50 - Pitch: 10 -\> 255 -
Mouth: 1 -\> 255 - Throat: 255 -\> 1

Current Python behavior overrides historical notes.

Console diagnostics should make all four voice parameters easy to
inspect.

## Speech input

Phrase files support:

    S:HELLO THERE
    P:AY4 AEM AH ROH4BOWLAORD

-   `S:` = normal text through SAM reciter
-   `P:` = SAM phonetic input
-   `#` lines = comments

The Python implementation has SAM-phoneme-aware splitting/tokenization.
SAM phonemes include one- and two-character phonemes, stress markers
1--8, and punctuation such as `.`, `,`, `?`, and `-`.

Never split phonetic input arbitrarily inside a SAM phoneme.

`safe_say` and `safe_say_phonetic` partly worked around MicroPython heap
limits. Do not mechanically reproduce those workarounds if C can avoid
them; preserve behavior instead.

## Operating modes

The current Python reference is authoritative.

### Normal Mode

-   Pot 1 = Speed
-   Pot 2 = Pitch
-   Phrase buttons speak assigned phrases.

### Voice Mode

Historically Button 16. - Pot 1 = Mouth - Pot 2 = Throat - Later
behavior is toggle-based rather than hold-based. - Confirm exact Button
16 behavior against current reference.

### Sample Rate Mode

Historically Button 13. - Pot 1 controls live playback sample rate. -
Pot 2 follows current reference behavior. - Selected sample rate
persists after leaving the mode. - Historical range: 8000--44100 Hz. -
Preserve current bit-depth controls if present.

### Phrase Bank / Preset Mode

Historically Button 14.

Phrase banks: - Hold Button 14 + Button 1 -\> `phrases_1.txt` - ... -
Hold Button 14 + Button 12 -\> `phrases_12.txt`

Button 14 pressed/released alone enters Preset Mode.

Preset Mode: - Short press 1--12: recall corresponding preset and
exit. - Hold 1--12 for about 2 seconds or more: store corresponding
preset. - Presets include Speed, Pitch, Mouth, Throat, Sample Rate, plus
any additional current parameters in the reference.

SAM historically speaks: - "preset stored" - "preset loaded"

Do not replace spoken confirmations with console-only messages.

### Chromatic Pitch Mode

Historically Button 15. - Pot 2 controls base pitch. - Buttons 1--12
apply chromatic C-through-B semitone offsets. - Preserve ratios/mapping
from current Python. - Derive note pitch from base pitch; avoid
cumulative error.

### Button 16

Button 16 evolved during development as shift/voice control. Use the
current Python implementation as source of truth.

## Editable phrase/config files

Users must be able to edit phrase/configuration text files without
recompiling firmware.

Expected phrase banks: - `phrases_1.txt` through `phrases_12.txt`

Other persistent data includes presets, locks, and current configuration
files in the Python project.

Do not permanently replace editable files with compile-time C arrays.
Embedded factory defaults are acceptable only as fallback.

## Persistent storage and USB editing

Desired final design: - Reserve external QSPI flash for user storage. -
Use an appropriate filesystem/storage design. - Expose editable files
over USB mass storage. - Keep USB serial where practical. - Users should
edit files on Mac/PC without reflashing.

Never allow Pico firmware and USB host to independently write the same
filesystem concurrently.

A dedicated USB edit/config mode is acceptable: 1. Stop/unmount firmware
access. 2. Give host exclusive mass-storage access. 3. User edits and
ejects. 4. Remount and reload files.

Keep storage behind a clean API so synth/control code is independent of
embedded defaults, LittleFS, FatFS, or USB-backed flash.

## Parameter locks and modulation

The Python version contains phrase/parameter locks and word-by-word
modulation. These are part of the feature set and must not be silently
omitted.

Study the current Python implementation and reproduce its semantics.
Keep parsing/state separate from SAM rendering where practical.

## Real-time rules

Audio is the highest-priority real-time task.

Avoid: - long blocking sleeps - dynamic allocation in timing-critical
paths - filesystem writes during critical audio timing - CPU-driven
sample timing where PIO/DMA is appropriate - blocking button scans that
stall speech

Prefer: - static/fixed-size buffers - bounds checking - PIO/DMA -
nonblocking state machines - deterministic memory use - short
control-loop iterations

Establish functional parity before aggressive optimization.

## Memory and safety

RP2040 RAM is limited.

-   Avoid unnecessary phrase/PCM copies.
-   Bounds-check text and phoneme buffers.
-   Never use unbounded `strcpy`, `strcat`, `sprintf`, etc.
-   Prefer `snprintf` and explicit sizes.
-   Check phrase line lengths.
-   Reject malformed config safely.
-   Keep SAM buffers deterministic where practical.
-   Treat user-edited files as untrusted input.

## Architecture

Keep concerns separated. Preferred conceptual layout:

    src/
      main.c
      pvs.c / pvs.h
      controls/
        matrix.c / matrix.h
        pots.c / pots.h
      audio/
        audio.c / audio.h
        *.pio
      sam/
        SAM wrapper/engine
      storage/
        phrases
        presets
        locks
        filesystem/USB
      leds/
        ws2812.c / ws2812.h
        *.pio

A flatter existing layout is acceptable. Refactor only when useful.

## Source-of-truth policy

Resolve behavior in this order:

1.  Explicit current user request
2.  Current working `reference_python/`
3.  Validated C-port behavior
4.  Historical notes in this file

Do not change user-visible behavior merely because another
implementation is easier.

For each ported feature: 1. Locate complete Python behavior. 2. Identify
state, timing, file format, and hardware dependencies. 3. Implement
bounded C equivalent. 4. Add useful USB diagnostics. 5. Build for
RP2040. 6. Test hardware. 7. Then optimize/refactor.

## Build requirements

Build using Pico SDK without manually modifying generated dependencies.

Typical command:

    cmake --build build

Expected products: - `pvs.elf` - `pvs.bin` - `pvs.hex` - `pvs.uf2`

Successful compilation is not proof of correct hardware behavior.

## Diagnostics

Use concise USB serial diagnostics during bring-up.

Useful startup information: - PVS/SAM version - target MCU/board - CPU
clock - audio pin/rate - voice parameters - phrase bank - storage
state - matrix config - ADC/pots - PIO state machines - DMA channels -
filesystem/edit mode

Do not flood USB serial from real-time loops.

## Testing order

1.  Pico SDK build/UF2
2.  USB serial
3.  Basic GPIO
4.  SAM text -\> PCM
5.  GP0 audio
6.  Correct default speech
7.  Speed/pitch/mouth/throat
8.  Pots
9.  Matrix
10. Phrase files
11. Modes
12. Chromatic pitch
13. Phrase banks
14. Presets
15. Parameter locks/modulation
16. WS2812 UI
17. PIO/DMA optimization
18. Persistent flash filesystem
19. USB mass-storage editing
20. RP2350 validation

When something fails, reduce the test to the smallest subsystem rather
than changing multiple systems at once.

## C conventions

Favor straightforward embedded C.

-   Use `stdint.h` fixed-width types where widths matter.
-   Use `bool` for boolean state.
-   Public declarations in headers.
-   Module-private state/functions `static`.
-   Descriptive constants.
-   Enums for modes/state.
-   Avoid unexplained GPIO magic numbers.
-   Document PIO/DMA ownership.
-   Check meaningful return values.
-   Keep IRQ handlers short.
-   Correctly synchronize shared IRQ/DMA state.
-   Avoid C++ unless compelling; project should primarily be C.

## PIO/DMA ownership

Original architecture avoided conflicts: - SAM audio: PIO0 SM0 - Matrix:
PIO1 SM0 (`sm_num=4`) - DMA arranged so audio/scanners do not collide

Explicitly document/allocate: - PIO instance - state machine - DMA
channel - DREQ - IRQ usage

Prefer SDK claiming of unused DMA channels unless deterministic
assignment is required.

## Performance

Original MicroPython used 250 MHz partly because of Python overhead.

Do not assume native C requires 250 MHz forever. Initially preserve
known behavior, then measure whether standard clock is sufficient.

Any clock change must test: - SAM rendering throughput - audio
continuity - matrix response - ADC response - WS2812 - USB stability

## RP2350 / Pico 2

Keep Pico 2 support in mind.

Prefer Pico SDK APIs over unnecessary RP2040 register assumptions.
Isolate architecture-specific code behind common APIs/macros.

Avoid separate unrelated firmware trees.

## Non-goals

Do not: - replace SAM with another speech engine - remove phonetic
phrases - permanently compile all phrases into firmware - remove
presets/locks for convenience - require a Debug Probe - require an SD
card - convert to C++ without reason - silently change button
numbering/mode semantics - treat generated `build/` files as source

## Definition of successful port

Native Pico SDK firmware should reproduce practical functionality of the
working MicroPython PVS, including:

-   SAM speech
-   normal and phonetic phrases
-   speed/pitch/mouth/throat
-   live sample-rate behavior
-   current UI modes
-   chromatic pitch
-   phrase banks
-   presets
-   parameter locks/modulation
-   soft takeover
-   WS2812 feedback
-   robust matrix/pot handling
-   persistent editable files
-   convenient USB editing
-   reliable RP2040 operation

After RP2040 parity, validate Pico 2/RP2350 from the same source tree.

## Guidance for coding agents

Before substantial changes: - inspect relevant C files - inspect
corresponding Python reference - understand complete behavior - document
necessary assumptions

When fixing build errors: - fix the source-controlled cause - do not
patch generated build output as the final solution - do not hide defects
by weakening compiler behavior

When a feature is incomplete: - state that clearly - do not present a
stub as feature-complete

Preserve working functionality and progress incrementally.

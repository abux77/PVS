# PVS hardware companion

Serve this folder locally and open it in desktop Chrome or Edge:

```sh
python3 -m http.server 8877 --bind 127.0.0.1 --directory web
```

Open http://127.0.0.1:8877, click **Connect PVS**, and select the instrument's
USB serial port. After that first permission grant, the page automatically
connects to an available PVS on page load and when USB is plugged back in.
Manual Disconnect pauses automatic connections until you reconnect or reload.
If several authorized PVS ports are present, use Connect PVS to choose one.
Close other serial monitors and PVS tabs first. The instrument needs the
companion-enabled firmware. A firmware without it produces a timeout with an
update instruction. The page shows and edits all six voice parameters. Synthesis runs on PVS;
play phrases on the instrument.

The native 128×64 canvas uses exact white/black pixels from the OLED driver.
On a PVS without an OLED, it shows the firmware’s virtual display and keeps
all six controls available. The connection label reads VIRTUAL DISPLAY.
The default size is 512×256 CSS pixels (4×4 per hardware pixel); zoom options
are 5×, 6× and 8×. Narrow viewports scroll instead of shrinking the display.
The page starts blank. After disconnecting it keeps the last frame, clearly
marked as disconnected, and disables parameter editing. Device status reads
existing USB console diagnostics.

## Six live parameters

Speed (1–255), pitch, mouth and throat (0–255), sample rate (8000–88200 Hz),
and bit depth (1–8) are always displayed together. Sliders and number fields
send edits when released/committed, and display firmware-confirmed values.
Knob changes, preset recalls and mode changes update the controls automatically.

A parameter is editable when its physical knob has not picked up control in
its current mode, and it has no active phrase lock. Once a knob moves at least
the existing 800/65535 threshold, hardware owns that parameter until a mode
change or preset recall rearms pickup. Switch the hardware to another mode to
free those parameters. Firmware enforces this even if a knob moves between
telemetry and a web edit. Accepted edits rearm an assigned waiting knob at its
current raw position; stationary knobs do not overwrite web values.

The readouts show the selected base voice, matching the OLED; phrase locks
can temporarily override playback and are marked PHRASE LOCK. SAM timbre and
bit-depth changes apply at the next word/phrase boundary. Sample rate can also
change during playback. Web edits are session values and do not write flash;
use the existing hardware preset workflow to save them. Editing is disabled
while USB owns the storage or a firmware reboot is pending.

## Linux: “The device has been lost” immediately after connecting

A serial monitor (including Python pyserial) can leave a tty with `VMIN=0`
and `VTIME=0`. Some Chromium versions treat the resulting empty nonblocking
read as a disconnected device, even though PVS is still online. With other
serial monitors closed, restore the read setting, then reconnect in the page:

```sh
stty -F /dev/ttyACM0 min 1 time 0
```

Use the actual PVS tty path if it differs. Diagnostic scripts must restore
`VMIN=1`, `VTIME=0` after closing their serial port before handing it back to
the browser. No firmware reflash is required for this host-side issue.

For a local server that remains available after its terminal closes, Linux
systems with a user systemd session can use this from the repository root:

```sh
systemd-run --user --unit=pvs-companion --collect \
  python3 -m http.server 8877 --bind 127.0.0.1 --directory "$PWD/web"
```

Stop it with `systemctl --user stop pvs-companion` before starting another
server on port 8877.

## Parameter protocol v1

`params` reads the six values and ownership. `set <name> <integer>` edits one
of `speed`, `pitch`, `mouth`, `throat`, `rate`, `bits`. Both respond with:

```text
PVS PARAMS 1 <speed> <pitch> <mouth> <throat> <rate> <bits> <hardware-mask> <lock-mask> <busy> <editable> <mode> <result>
```

Mask bits 0–5 correspond to the parameter order above. Mode values 0–6 mean
Normal, Voice, Sample, Chromatic, Preset, USB edit, firmware update. Results
are `ok`, `hardware`, `locked`, `range`, `unavailable`, `parameter`, or `syntax`.
A rejection leaves values unchanged and includes the current state. Numeric
input is bounded before conversion and before assignment. Each response is
bounded and nonblocking, like OLED responses below.

The browser serializes requests, sends queued edits between complete OLED
snapshots and reads parameters after every snapshot. It never performs writes
from incoming telemetry. Firmware without parameter support prompts an update.

## OLED protocol v1

ASCII, newline terminated, on the existing CDC port at 115200 baud. Existing
console commands retain their behavior. No unsolicited display stream is used.

- Send `oled 0` to capture a coherent snapshot of 1024 bytes, last successfully
  transmitted panel inversion, and OLED online state. Capture increments the
  sequence number. The pixels are SSD1306 page order, least-significant bit
  at the top of each eight-pixel column.
- Request `oled 1` through `oled 15` in order to retrieve the remaining chunks
  of the same frozen snapshot. Each chunk has 64 bytes.
- Response: `PVS OLED 1 <sequence> <chunk> <inverted:0|1> <online:0|1> <128 hex digits>`.
- Each command emits at most one small packet into the TinyUSB TX queue when
  sufficient space is available; it never waits for the host. The browser
  disconnects with an explanation after a 1.5-second response timeout.
- Chunks outside 0–15 and continuation requests before a snapshot are ignored.
- Snapshots represent the panel's last transmitted bytes when online, or the
  current virtual framebuffer and inversion when no panel is online. During OLED dirty
  updates they can include the same partially updated image as the hardware.
  Only a complete, matching set of chunks is rendered by the browser.

The browser requests another snapshot 100 ms after finishing the previous one.
This is a sampled mirror; very short transients between samples may be missed.
All copying and USB work happens on core 0, using a fixed 1024-byte snapshot.
The driver adds no I2C transfers, IRQ work, or changes to audio resources.

## Verification

`node --test tests/test_web.mjs` checks page decoding and malformed packets.
`python3 tests/test_oled.py` checks driver snapshots and panel inversion alongside
existing display tests. `python3 tests/run_tests.py` runs firmware host checks.
`python3 tests/test_controls.py` also covers inactive edits, movement races,
pickup rearming, chromatic ownership, phrase locks and word-boundary changes.
For optional browser integration checks, install Playwright and run
`python3 tests/test_companion_browser.py` with the local page server running.
A Pico SDK build and hardware USB validation are required before flashing.

## Visual identity

The companion follows the supplied PVS v0.4 user guide: charcoal, blue,
pale white and red accents, with the supplied Audiowide font served locally.
The original retro PVS logo is displayed in white using CSS inversion.
See [asset credits and licenses](assets/README.md). OLED pixels retain the
firmware's monochrome rendering.

## Phrase grid and hardware mode

The toolbar shows the current hardware mode from `params`. The twelve buttons
below the controls follow the printed keypad order: 1–9, *, 0, #. Each shows
its loaded text or phonemes, input type, and any Speed/Pitch, Mouth/Throat, or
Rate/Bits lock groups. Empty slots are disabled. Clicking plays/retriggers that
bank phrase, independently of the hardware knob mode. Playback highlights the
active phrase; short phrases may complete between browser polls.

`bank` returns `PVS BANK 1 <bank> <revision> <playing-key:-1|0..11>`.
`phrase <key:0..11> <chunk:0..7>` returns:

```text
PVS PHRASE 1 <bank> <revision> <key> <chunk> <byte-length> <S|P|E> <lock-mask> <hex-or-dash>
```

Each chunk contains up to 48 UTF-8 bytes, hex encoded; `-` denotes no bytes.
S is text, P phonetic, E unassigned. Lock bits 1, 2, 4 mean Speed/Pitch,
Mouth/Throat, and Rate/Bits. The revision changes when the bank is loaded or
locks change. The browser checks it every poll and replaces the grid only
after collecting a complete matching bank. Text is rendered as text, never
HTML. No filesystem reads occur while streaming this in-memory metadata.

`key <key:0..11>` queues a monophonic phrase trigger and returns `PVS PARAMS`
with an acceptance/rejection result. MIDI notes use the same trigger path.
The grid is disabled during USB editing, firmware reboot and disconnection.
USB MIDI CC/note assignments are in the page's MIDI mapping panel and the
[firmware README](../README.md#usb-midi-input).

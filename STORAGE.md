# Editable files and USB

The firmware reserves the final 1 MiB of onboard QSPI flash for a FAT volume
named `PVS`. The linker rejects firmware that overlaps this region. Standard
UF2 firmware updates preserve this volume; a full-chip erase does not.

On completely blank storage, the first boot formats FAT12 and copies every
text file from `data/`. These embedded copies are provisioning defaults and a
read-only fallback if storage cannot mount. Later boots load the editable
files. A nonblank, damaged or foreign filesystem is never automatically erased.

Connect a USB serial terminal to the Pico and send these commands followed by
Enter:

- `bootsel`: stop audio and enter firmware update mode after storage is closed.
  If USB editing is active, the request waits for host eject. B+C does the same.
- `status`: print storage ownership, voice parameters and hardware/UI state.
- `edit`: wait for current speech to finish, then make the `PVS` disk available
  to the computer. Speech and normal instrument controls pause during editing.
- `help`: print the command summary.
- `format`: erase the reserved PVS filesystem and install all factory files.
  This destroys existing PVS files. It waits for speech to finish and is refused
  while USB edit mode is active. Use it to replace leftover MicroPython storage
  when those old files are no longer needed, then send `edit` to open the disk.

Edit and save files on the disk, then eject the `PVS` disk using the operating
system. SCSI eject flushes the flash cache, removes host access, remounts the
firmware filesystem and reloads configuration. USB serial stays available.
Disconnecting without ejecting does not grant the firmware filesystem access.
If USB is disconnected while external power remains connected, reconnect and
eject, or restart the instrument after the host has relinquished the disk.

The instrument and host never mount the same filesystem concurrently. The MSC
interface reports no medium during normal operation. Firmware reads, preset
stores and lock stores are rejected while USB owns the disk. Flash writes run
on core 0 only after audio stops; core 1 is unused.

## Files

| File | Format |
| --- | --- |
| `phrases_1.txt` … `phrases_12.txt` | One phrase per nonempty, noncomment line. `S:` means text, `P:` means SAM phonemes; an unprefixed line is text. Prefixes ignore case. |
| `presets.txt` | `key,speed,pitch,mouth,throat,sample_rate,bit_depth` |
| `locks_1.txt` … `locks_12.txt` | `key,flags,speed,pitch,mouth,throat,sample_rate,bit_depth` |
| `startup_state.txt` | `bank,last_recalled_preset` |

Preset and phrase keys are 1–9, *, 0 and #. Legacy numeric 10/11/12
are accepted as aliases for */0/#. Bank filenames remain numbered 1–12. Lock flags are 1 for speed/pitch, 2 for
mouth/throat, and 4 for sample rate/bit depth; add flags to combine groups.
Missing lock files mean no locks. Blank lines and lines beginning with `#` are
ignored, except that `#,` starts a hash-key lock/preset record. Whitespace and CRLF line endings are accepted.

Six-column legacy presets default to 8 bits. Seven-column saves retain bit
depth across restarts, fixing the Python writer's omission of that field.
Bit depth is clamped to 1–8 as in the reference. Invalid numeric rows are
ignored: speed must be 1–255, pitch/mouth/throat 0–255 and sample rate
8000–44100 Hz. Integers are checked before narrowing to the C voice fields.
Startup defaults are bank 1 and no recalled preset if the file is absent or
malformed. The last valid duplicate preset/lock row wins.

Each phrase can contain at most `PVS_MAX_PHRASE - 1` bytes (currently 383).
At most `PVS_MAX_PHRASES` rows (currently 16) are accepted; buttons use the
first 12. Oversized or binary phrase lines reject the complete bank, retaining
the previous bank and button assignments. Phrases are never truncated into
partial phonemes. Filenames are case insensitive and use FAT long names.

## Persistence and limitations

The backend has a fixed 4 KiB erase-sector cache; the USB transfer buffer is
also 4 KiB. It checks block bounds, avoids programming unchanged data, and
verifies each flash commit. Host writes are committed before acknowledgement.
Preset, lock and startup saves use a temporary file and a backup; if the final
filename is missing after an interrupted replacement, the backup is loaded.
Unchanged startup selections do not cause additional writes.

FAT is not journalled, and this direct flash backend does not implement wear
levelling. Power interruption during a flash erase can damage filesystem
metadata. Keep backups of edited files, eject before disconnecting, and use
normal instrument stores rather than continuously rewriting configuration.
On mount failure the instrument uses factory fallback and exposes the existing
volume in edit mode for host repair. Reformat as FAT if recovery is needed;
exFAT is not supported. A host reformat creates an empty volume, so restore
the desired text files from `data/` or a backup afterward.

Host tests cover real FatFs formatting, initial provisioning, all phrase banks,
preset/lock round trips, bit depth, startup persistence, exclusive USB ownership,
raw host edits and eject/remount, input bounds, backup recovery and preservation
of damaged volumes. USB enumeration, OS eject behavior, flash endurance and
power interruption still require physical hardware validation.

Press A and D together to enter USB edit mode without serial. This stops
speech before handing the volume to the host. Release both buttons, edit and
save, then eject the disk to resume. The chord does not change UI modes.

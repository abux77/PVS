# Working on PVS

Read `AGENTS.md` and the current controls in `README.md`. Keep fixes in source
control, never only in generated build dependencies.

## Verify a change

1. Run `python3 tests/run_tests.py` with Python 3 and a C compiler supporting
   AddressSanitizer and UndefinedBehaviorSanitizer.
2. Configure/build Pico with SDK 2.3.1 as described in the README.
3. Configure/build Pico 2 in a separate directory for portable changes.
4. Use `docs/HARDWARE_VALIDATION.md` for relevant device checks. Report what
   was actually tested on hardware separately from successful compilation.

Normal builds and host tests do not require Pillow. Regenerating OLED font/logo
headers and previews requires Pillow; commit the regenerated headers with
their source/generator changes.

## Preparing a GitHub upload or release

- Keep local editor settings, diagnostics, build trees and credentials out of
  commits; the root `.gitignore` covers these local directories.
- Review `data/`, and artwork for material you intend
  to publish. Factory phrases ship in both source and firmware.
- Review `THIRD_PARTY_NOTICES.md`; original code is MIT; logo, fonts and third-party code have separate terms.
- Attach board-specific UF2 files to releases and identify the source commit.
  Local build numbers alone do not identify a source revision.
- Preserve user storage when updating firmware; never require formatting for
  ordinary firmware updates.

The GitHub workflow runs host tests. Hardware and cross-target firmware builds
remain separate validation steps.

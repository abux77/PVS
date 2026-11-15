# Third-party notices and license status

This inventory records the notices supplied with the local source/assets.
It does not grant additional rights or replace upstream terms.

## SAM

The renderer in `third_party/sam/` comes from s-macke/SAM at
`a7b36efac730957b59471a42a45fd779f94d77dd`.
[Provenance and local changes](third_party/sam/README.pvs.md) and the
[upstream README](third_party/sam/UPSTREAM_README.md) are retained.
The upstream License section does not supply a standard open-source license.
Do not represent SAM as MIT/BSD licensed.

The C reciter/parser and related tables derive from the supplied Python SAM
reference. No separate license grant was supplied for that reference.

## FatFs

FatFs R0.15 by ChaN is vendored in `third_party/fatfs/`.
Keep the copyright and redistribution terms in `ff.c`, `ff.h`, and
`ffunicode.c`. See its [integration notes](third_party/fatfs/README.md).

## OLED font and artwork

Audiowide is Copyright (c) 2012 Brian J. Bonislawsky DBA Astigmatic (AOETI), distributed
under the [SIL Open Font License 1.1](assets/fonts/OFL.txt).
The source TTF is from google/fonts, `ofl/audiowide/Audiowide-Regular.ttf`.
The generated `src/oled_font.h` is a derivative named **PVS Display**, with fixed-width
digits and remains under that license. Preserve the copyright and OFL text
when redistributing the font or bitmap with firmware.
See [font provenance](assets/fonts/README.md).

The browser companion also serves the supplied, unmodified Audiowide TTF.
Its license and the supplied retro PVS logo credits are included in
[web asset notices](web/assets/README.md).

The PVS logo and its generated OLED bitmap/preview are Copyright 2026
Andrew Buckie, licensed under [CC BY-NC 4.0](assets/LOGO_LICENSE.md).

## Pico SDK and project code

The SDK is an external build dependency, not vendored here.
Preserve the notice in `pico_sdk_import.cmake`.
Andrew Buckie's original PVS code and documentation are licensed under the
[MIT License](LICENSE). This grant excludes third-party code/data, fonts, and
the separately licensed logo. SAM-derived C code and tables retain the
unresolved upstream status described above, even though the Python reference
has been removed. Regression fixtures retain captured behavior without
requiring the Python implementation.

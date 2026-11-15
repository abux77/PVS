# OLED font

Audiowide Regular, Copyright (c) 2012 Brian J. Bonislawsky DBA Astigmatic (AOETI).
Source: https://github.com/google/fonts/tree/main/ofl/audiowide
License: SIL Open Font License 1.1, preserved in OFL.txt.
Audiowide is a Reserved Font Name; the modified bitmap is named PVS Display.

Generate the PVS Display bitmap with tools/generate_oled_font.py (Pillow required).
The derivative uses 16-pixel-high glyph cells and fixed-width digits.
Strokes are thickened downward by one pixel without changing letter spacing.
Numeric zero padding is applied by the firmware UI, independently of the font.

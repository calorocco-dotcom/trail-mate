# Music content-font fixture

`font.bin` is a small external LVGL binfont subset of Noto Sans CJK SC Regular,
generated from `tools/fonts/NotoSansCJKsc-Regular.otf` at 16 px / 2 bpp with
`tools/generate_binfont_with_lv_font_conv.py --no-compress --no-kerning`.
The source font uses the SIL Open Font License 1.1; its embedded license and
copyright notices are reproduced in `LICENSE.txt`.

The native Music test installs this subset as a **content-only** font pack in
its in-memory filesystem before initializing the English UI. It verifies that
Chinese metadata, lyrics, library categories, playlist names and widget labels
use the actual content fallback and resolve real glyphs. This file is never
compiled into device firmware or deployed as a user language pack.

# Artwork regression fixtures

`color-bars.jpg` and `color-bars.png` are original generated red, green, blue and
white bars. Regenerate them with `python generate_color_bars.py` and Pillow.
Each bar is 16 pixels wide; the JPEG uses baseline encoding without chroma
subsampling. The parser/cover test compares decoded RGB565 channel values at bar
centres and permits one quantization step for JPEG. The images distinguish the
bundled LVGL TJpgDec BGR output from the PNG decoder's RGBA output.

The other images exercise square/landscape geometry, JPEG/PNG input and rejection
of progressive JPEG. Their pre-existing geometry checks are retained.

# Music and widget screenshot sources

These are actual LVGL native previews from production UI code. Metadata, cover
art and service state are sample inputs. They are not camera captures or proof
of audio quality on a physical device.

The initial Music captures were added in source `3df8082d`. They cover Pager
Now Playing/paused, a T-Deck interruption, and the wider P4 widgets. The Pager
Quick controls image was refreshed on October 17, 2026 using the current native
preview and `tests/site/pager-input-native.test.mjs`; it shows keyboard focus
after exercising every switch and both sliders. Pager navigation and Help
behavior is checked through actual LVGL keypad/encoder input devices.

Large artwork and visible widget resources use PSRAM on supported hardware.
The native previews simulate those allocation and service contracts; firmware
builds and hardware reports remain separate evidence.

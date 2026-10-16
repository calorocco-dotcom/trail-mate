# T-Display-P4 Final Display Runtime Architecture

## Status and scope

This document defines the display runtime constraints for Trail Mate's ESP32-P4
T-Display-P4 TFT (HI8561) and AMOLED (RM69A10) targets. It replaces the historical
application-level three-framebuffer presenter. Do not reintroduce an independent
front/back swapchain, software full-frame rotation or DPI panel-owned LVGL buffer.

The scope is the display data path. Panel timing, MIPI-DSI lane/bit rates, panel
drivers, touch I2C, SD, NVS, LoRa and business UI are outside this architecture
replacement.

## Goals

The display must remain stable while UI animation, SD I/O and NVS/Flash commits
run concurrently. Use the ESP32-P4 PPA rather than rotating and copying entire
frames through the CPU and PSRAM. The display runtime must:

- Give LVGL and the DPI driver clear, non-overlapping buffer ownership.
- Use LVGL `PARTIAL` rendering for dirty areas.
- Use PPA for 90-degree rotation.
- Complete the LVGL flush when color transfer completes.
- Let the DPI driver handle transfer rather than treating it as an
  application-level swapchain.
- Use the same display memory model on TFT and AMOLED targets.

## Final data flow

```text
LVGL logical dirty area (landscape UI)
        |
        v
Independent LVGL draw buffer (PSRAM | DMA)
        |
        v
esp_lvgl_port PPA rotation (blocking PPA operation)
        |
        v
PPA cache-line-aligned output buffer
        |
        v
esp_lcd_panel_draw_bitmap(rotated dirty area)
        |
        v
DPI / DMA2D / MIPI-DSI
        |
        v
on_color_trans_done -> lv_display_flush_ready()
```

`buffer_size` may cover the complete physical display to accommodate the largest
dirty area. This does not imply full-frame rendering. Disable `full_refresh` and
`direct_mode`, so LVGL uses `PARTIAL` rendering.

## Ownership and synchronization contracts

| Resource | Owner | Usage rule |
| --- | --- | --- |
| LVGL draw buffer | `esp_lvgl_port` | Allocate with PSRAM + DMA capabilities as independent LVGL rendering input. |
| PPA output buffer | `esp_lvgl_port` PPA helper | Align to the L2 cache line and hold only the current rotated dirty area. |
| DPI driver internal resources | ESP-IDF DPI driver | Set `num_fbs = 0`; the application must not obtain, retain or swap panel framebuffers. |
| LVGL flush lifecycle | `on_color_trans_done` | Call `lv_display_flush_ready()` only after color transfer completes. |

The runtime therefore has no `front_buffer`, `pending_front_buffer`,
`awaiting_refresh`, refresh-boundary semaphore or application back-buffer
selection state.

## Implementation constraints

### DPI panel

- `dpi_cfg.num_fbs` must be `0`.
- `dpi_cfg.flags.use_dma2d` must remain `true`.
- `lvgl_port_display_dsi_cfg_t.flags.avoid_tearing` must be `false`.
- Do not call `esp_lcd_dpi_panel_get_frame_buffer()`.
- Do not drive LVGL flush completion through `on_refresh_done`.

### LVGL and PPA

- Both T-Display-P4 targets must set `CONFIG_LVGL_PORT_ENABLE_PPA=y`.
- Create the display through `lvgl_port_add_disp_dsi()`.
- Set `buff_spiram=true`, `buff_dma=true` and `sw_rotate=true` in the display
  configuration. On ESP32-P4 with PPA enabled in Kconfig, this historical
  `esp_lvgl_port` field invokes PPA rather than `lv_draw_sw_rotate()`.
- Set `double_buffer=false`, `full_refresh=false` and `direct_mode=false`.
- Establish logical orientation with
  `lv_display_set_rotation(display, LV_DISPLAY_ROTATION_90)`.
- The runtime must not directly include `driver/ppa.h` or register its own PPA
  client. The pinned `esp_lvgl_port` implementation manages the PPA client,
  output buffer, alignment, dirty-area coordinate transformation and
  `draw_bitmap()` calls.

## Prohibited legacy architecture

Do not restore any of these patterns as a fallback, feature flag or conditional
compilation branch:

- `P4DsiRotatingPresenter`.
- `dpi_cfg.num_fbs = 3`.
- A panel-owned logical LVGL buffer.
- A custom front/back/logical three-framebuffer swapchain.
- `LV_DISPLAY_RENDER_MODE_FULL`.
- `lv_draw_sw_rotate()`.
- A full-screen `esp_lcd_panel_draw_bitmap()` on every flush.
- `on_refresh_done -> lv_display_flush_ready()`.

## Touch and business boundaries

Touch continues to bind physical coordinates to the same `lv_display_t`.
LVGL display rotation maps the physical portrait panel to the logical landscape
UI. This architecture does not require changes to touch-controller readings,
I2C locks, keyboard input, SD or NVS code.

## Acceptance requirements

Every change to the P4 display runtime must satisfy at least these requirements:

1. Both TFT and AMOLED targets configure and compile.
2. Architecture contract tests assert PPA is enabled, `num_fbs=0`,
   `avoid_tearing=false` and `lvgl_port_add_disp_dsi()`, and assert that the old
   presenter is absent.
3. Check startup rendering, touch at all four corners and edges, swiping,
   keyboard focus and UI animation on physical hardware.
4. With continuous UI animation, an NVS commit every 500 ms and continuous SD
   writes running concurrently, physical hardware shows no flicker, PPA errors
   or LVGL flush stalls.
5. After automated validation, audit the impact and confirm that changes affect
   only this architecture and its verification files.

# Shared dialog frame

`ui::components::dialog_frame` owns the common visual structure of a modal:
an input-blocking scrim, a centered panel bounded by its parent, a fixed title
bar, a compact close button, and a vertically scrolling body. The title bar
uses the same `TopBar` factory, profile height, font and theme as page headers.
Long body content scrolls without moving the title or its close button.

Pages supply a localized title, preferred dimensions, close and key callbacks,
and an optional callback context. The returned `Frame` is a view of LVGL
widgets, not a retained controller. Pages add their content to `frame.body`,
put `frame.close` in their existing focus group, and delete `frame.overlay`
when closing. They retain responsibility for restoring focus and stopping
their own timers or tasks. The close button supports touch and Enter. After
building the focus group, pages call `bind_close_shortcuts(frame, group)` so
X/x closes from any of this dialog's controls. The shared handler runs before
page key handlers. Page callbacks retain their existing Escape/Back behavior.
Controls in a separate nested dialog do not inherit the parent shortcut.

Short action lists reuse the flat popup-row style and call
`fit_content_height(frame)` after adding rows. This preserves the frame's
padding and removes unused vertical space while retaining viewport bounds
and scrolling. Closing is available in the title bar without a duplicate
Cancel row.

This uses composition rather than a hierarchy of dialog subclasses. There is
no global modal registry, copied business model, persistent dialog cache or
additional protocol buffer. Each page allocates only its currently open dialog.

## Current adopters

- Map Help, track/navigation file picker, and the Navigation, Tracks and
  Contours management dialogs.
- Tracker navigation creation, card actions, delete confirmation and preview
  Help.

Other legacy modal implementations remain separate until migrated; adding a
shared component does not by itself make every existing firmware popup use it.
New dialogs in these flows should use the shared frame instead of rebuilding
their own scrim, header and close control.

## Verification

`tests/tracker_card_view/test_dialog_frame.cpp` runs the real LVGL layout,
frame, TopBar and bottom-button implementations at 240, 320 and 480 pixels.
It checks header-height parity, panel bounds, compact close placement, fixed
header while scrolling, content-sized action lists, exactly-once touch/Enter/X/x
close, and widget cleanup.
Native preview tests exercise the real page flows separately. Native rendering
is not a physical-device capture.

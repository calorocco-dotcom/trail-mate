# Menu widget pages

Pager, T-Deck and Wio Tracker L2 share the same adaptive launcher grid. The
76-pixel-high cards use a 72-pixel minimum width and wrap according to available width: six columns on the
480-pixel Pager and four on the 320-pixel T-Deck/L2. Overflow scrolls vertically.
Pager no longer uses a separate large-card carousel.

Column widths divide the available width equally after reserving inter-column
gaps. Indivisible pixels are split between the outside margins, keeping their
difference within one pixel. Incomplete rows retain the same column tracks.
Keyboard focus uses a pale rounded tile with an inset border; the launcher
starts below the top bar with an explicit gap. The compact viewport spans the
space between the fixed top and bottom bars; padding preserves the initial
whole-row centering. Moving icons remain visible until they reach those bars.
At 320 x 240 the viewport is y=24..215, rather than the earlier y=41..198;
the first two rows retain their initial y=41 and y=123 positions.

Compact cards scale their image content with `LV_IMAGE_ALIGN_CONTAIN` inside
the existing flex slot. A whole-object style transform is inappropriate here:
the flex slot can be shorter than the source, so source rows are clipped before
the object is scaled. The corrected path draws the complete source with its
aspect ratio preserved, including in focused states. It creates no resized
bitmap, screenshot buffer or additional UI object. Large-touch and carousel
profiles retain their separate icon behavior.

## Navigation

| Surface | Input | Result |
| --- | --- | --- |
| T-Deck/L2 launcher | Swipe right | Open the negative screen |
| T-Deck/L2 negative screen | Swipe left, D, Right, Escape or Back | Return to the launcher |
| Negative screen | Swipe up/down, W/S, Up/Down | Next/previous widget |
| Pager launcher or negative screen | V | Switch between the launcher and negative screen; preserve the selected app on return |
| Pager launcher | W/A/S/D | Move through the icon grid |
| Pager launcher | Encoder rotation | Change the focused application icon |
| Pager negative screen | Encoder rotation | Next/previous widget |
| Pager negative screen | A/D, then Enter or encoder press | Select and activate a widget control; enter or finish slider editing |
| Pager negative screen | Escape or Back | Return to the launcher |
| P4 right dashboard | Swipe left/right | Next/previous widget page |

Pager keeps `H Help` and `V Widgets` visible in the launcher footer; the latter
changes to `V Menu` on the negative screen. H opens the matching help with V as
its first row. V also works while this help is open: it closes the dialog and
switches screens. A at the left edge stays within the launcher grid. While
editing a widget slider, A/D adjusts the value by five percentage points;
W/S and encoder rotation continue to switch widgets. Application pages and
text editors retain their own shortcuts and literal text input. See
[Pager keyboard and encoder navigation](pager-input.md) for the complete rules.

The negative screen shows exactly one widget: Mesh, GPS, Recent Activity,
Compass, Agenda, Music or Quick controls. Navigation stops at the first/last widget. P4 keeps the
left launcher and displays four status widgets on its first right-hand page,
then Agenda, Music and Quick controls on separate pages. Footer controls provide a visible alternative to
gestures. Widget browsing does not activate a background launcher icon.

A drag is owned by its original menu gesture until physical release. Leaving
the initially pressed control is not cancellation of that gesture. Recognized
swipes cancel the original click before replacing any controls. App screens,
sleep scenes and modal focus groups retain their own input ownership.

The negative screen and launcher follow horizontal finger movement before
release. A short or reversed gesture settles back to its original page; a
committed gesture settles to the adjacent page. P4 horizontal widget paging and
compact-screen vertical widget paging slide the outgoing view before building
the incoming view, so hardware leases never overlap between widget pages.
LVGL does not forward continuous PRESSING events to input-device listeners;
a 16 ms UI timer tracks the already sampled pointer only while pressed. The
timer and animations are cancelled when leaving or destroying the menu.

The launcher grid owns its vertical scrolling and does not chain native scrolls
to the menu owner. The menu owner has no native scroll direction. Feed transitions
cancel residual parent scroll animations and restore its zero scroll position
before starting and after settling; the custom translation is the only intended
horizontal offset. No snapshot, framebuffer or extra polling timer is added.

## Widget boundaries

Compass is included only when the target exposes a magnetometer through the
stable `orientation::heading_supported()` capability. Checking the capability
does not initialize a sensor or acquire a heading lease. IMU screen-rotation
availability and a missing/stale heading sample are separate facts. T-Deck,
Pager and Wio L2 currently omit Compass; their compact feed has six pages. P4
retains its four-card overview. A target without a magnetometer also omits the
Compass overview slot and expands Recent to fill that row. Page navigation is
derived from the filtered widget order, without an extra registry allocation.

- `dashboard_widget.h` defines the root/refresh handle, strict external-memory
  allocation and DELETE ownership. It contains no service data.
- Each `dashboard_*_widget.cpp` owns its private state, child controls, data
  reads and cleanup. No widget accesses `DashboardUi` or another widget's state.
- `menu_dashboard.cpp` owns selection, arrangement, gestures and one refresh
  timer. `dashboard_state.*` retains only this container's state in PSRAM.
- Widgets read the existing GPS, messaging, mesh, orientation and Agenda ports.
  They do not duplicate the background services. P4 heading uses a balanced
  demand lease; the final visible consumer releases its sampling task/handles.
- T-Deck, L2 and Pager bind the existing `IAgendaSource`. P4 creates a per-view,
  read-only adapter over the same Agenda file store and query use cases. It
  reads an existing `/agenda/events.dat` through the shared SD runtime, never
  mounts, formats, creates or repairs a store, and rejects external USB ownership
  or a changed media session. A missing file means an empty Agenda; corruption
  and I/O failure remain errors. This does not add a P4 Calendar editor or a
  second reminder service. Preview data comes only from a test adapter.

The container creates only visible widgets. Closing the negative screen,
changing a widget/page, or entering an app deletes those controls. Root DELETE
callbacks destroy each private state exactly once. Deleting/reinitializing the
container cancels its timer before its state disappears.

Animations move existing objects and allocate no screenshot or secondary
framebuffer. Their bookkeeping uses the same PSRAM UI allocation contract.
Native ESP-IDF consumes quick-control intents from its existing service pump,
outside the LVGL lock, including when UI dispatch belongs to the LVGL task.

## Memory policy

Container state, widget state, GNSS satellite/ranking arrays, recent-text caches
and the bounded Agenda snapshot are allocated with
`MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT` on ESP. Allocation failure returns no
widget; it never substitutes internal RAM. Large snapshots are not automatic
LVGL-task locals.

Arduino widgets use the existing `PsramUiLease` contract for LVGL allocation.
The unified menu grid is also created within that contract. P4 selects a custom
LVGL allocator that routes small and large UI allocations to PSRAM, including
subsequent label reallocations. Peripheral/DMA buffers retain their SDK policy.
Only small registry pointers remain in internal static storage. Existing
service APIs retain their own transient allocation behavior; this change does
not claim that the whole firmware or its stack has zero internal-RAM activity.

The shared container ticks once per second and schedules visible widgets at
one-, two- or four-second cadences. Agenda's bounded
query runs on opening and at most once per 30 seconds while visible. Hidden
Agenda widgets are destroyed and issue no queries. All widgets retain localized
text until it actually changes.

## Verification

`menu_icon_regression` extracts the actual card/image/label creation code and
renders 48-, 64- and 80-pixel test sources in LVGL 9.4. The red top band, green
body and blue bottom band pixel counts match an independent, unclipped source
render at the same scale in normal, focused and focus-key states. Before the
correction, a 64 x 64 source in a 64 x 49 flex object lost both test edge bands
(0 pixels each, versus 176 each in the reference). Hardware/catalog callbacks
are omitted from this pixel test; card/image/label layout is production code.
The full navigation/UI suite now passes 18 tests. Wio scroll and stationary-icon
acceptance were reported by the user in separate checks, as recorded below.

The stationary-icon Wio image built successfully in 201.287 seconds (exit 0),
with 90,124 B static RAM and 5,035,557 B linked Flash: unchanged static RAM and
84 B more Flash than the preceding scroll-correction image. The binary is
5,035,968 B, SHA-256
`7e56f6d4bb8d6ddd386c519e2476b60a0343a7a85ebe87b483de932c579f0bf3`.
The user then connected the Wio in download mode. The matching image was flashed
to the confirmed target and passed Flash hash verification in 61.653 seconds
(exit 0). On 2026-10-16, the user then checked the flashed Wio and confirmed that
the icon tops are now fully visible. This records user-reported acceptance of
the stationary top-clipping fix, without an additional screen capture or runtime
heap measurement. T-Deck and Pager physical acceptance remain unverified.

`tests/navigation_ui/menu_scroll_regression` links the real Dashboard code to
LVGL 9.4 and extracts the actual launcher/tile geometry from `menu_layout.cpp`.
Only widget hardware/service models and the app catalog are replaced. It covers
12 open/close cycles, reversed gestures, native parent-scroll animation recovery,
launcher vertical scrolling and exact viewport bounds at 320 x 240 and 480 x 222.
Before the correction, an injected residual scroll left the feed at x=48..367
after release; the patched code restores x=0..319. Ordinary synthetic swipes
alone did not reproduce the physical report. This proves recovery from that
condition, without claiming every physical trigger has been identified. The
scroll-regression stage passed 17 navigation/UI tests. Physical acceptance is recorded separately
below; these host results alone do not establish it.

The combined menu/preview-key Wio image built successfully on 2026-10-16 in
214.959 seconds (exit 0): 90,124 B static RAM and 5,035,473 B linked Flash.
Relative to the preceding reader/preview image, static RAM is unchanged and
linked Flash increases by 420 B. This is a build comparison, not a runtime
heap-peak measurement. The 5,035,888-byte binary has SHA-256
`10bb35a54d947034e174a70c52a5e881e796cf5fc830098187642572df5c03c8`.
Upload to the confirmed Wio and Flash hash verification completed in 61.661
seconds (exit 0). On 2026-10-16, the Wio user tested the flashed image and
confirmed that both horizontal Dashboard swipes and vertical launcher-icon
scrolling now work normally. This is user-reported physical acceptance of those
two fixes; no additional serial trace or runtime heap measurement was captured.
T-Deck touch and Pager physical-key acceptance remain unverified.

`tests/menu_widgets` tests the actual allocation/ownership helpers against real
LVGL, repeated destruction and explicit PSRAM failure. It separately compiles
the production IDF allocator and checks that even small allocations and failed
reallocations obey the external-memory policy.

The P4 Agenda adapter is tested with records produced by the actual storage
codec/service. Its fake shared-SD port deliberately has no write or mount API.
The test verifies strict PSRAM failure, missing/corrupt files, USB ownership,
removal, a card change during a query, successful retry and no retained handles
or adapter allocation after destruction.

`tests/site/menu-widgets-native.test.mjs` compiles/runs the real production UI
for Pager, T-Deck, L2 and both P4 panels. It checks adaptive columns, contextual
encoder behavior, touch/key navigation, one visible widget, hidden-query
suppression, lease release, repeated entry/exit and P4 paging. Satellite
distribution is checked on the first frame: the radar has zero coordinate
padding and resolves its dimensions before the first refresh. Setting
`WIDGET_UI_ARTIFACT_DIR` saves actual RGB565-rendered PNGs and layout coordinates.
The existing P4 lifecycle suite continues to exercise 250 container rebuilds
per panel.

The allocator tests also reproduce static archive ordering: the custom
allocator archive precedes the library that references it. P4 explicitly keeps
`lv_malloc_core` reachable so archive extraction cannot drop the allocator.
Arduino's orientation adapter reports absent heading hardware without starting
a task. On those targets, the compass displays GPS/team bearings rather than
describing a nonexistent magnetometer as still warming up.

Physical touch thresholds, encoder direction, internal/PSRAM heap peaks and
rendering latency still require device acceptance under live service load.

Music and quick-controls ownership, supported formats, resource limits and
failure semantics are documented in [Music and device controls](music.md).

## Actual LVGL captures

These frames are rendered by the production LVGL code with deterministic
preview services. Agenda entries below are preview fixtures, not device data.

![Pager: six symmetric columns and inset focus](../../images/menu-widgets/pager-menu.png)

![T-Deck and L2: four columns and whole rows](../../images/menu-widgets/tdeck-menu.png)

![P4: left launcher and paged right dashboard](../../images/menu-widgets/p4-menu.png)

![Pager negative screen: Agenda widget](../../images/menu-widgets/pager-agenda.png)

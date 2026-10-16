# Pager keyboard and encoder navigation

The Pager launcher keeps W/A/S/D for its adaptive icon grid. V toggles the
widget feed from any launcher position and restores the selected launcher
icon on return. A at the left edge no longer opens the feed. This shortcut is
scoped to the launcher: applications and text fields retain literal `v`.

## Widget feed

| Input | Action |
| --- | --- |
| V or Back | Return to the launcher |
| W/S or encoder rotation | Previous/next widget |
| A/D | Select a control within Music or Quick controls |
| Enter or encoder press | Activate the selected button; enter/leave slider editing |
| A/D while editing a slider | Decrease/increase its value by five percentage points |
| H | Open contextual launcher/widget help |

Music initially selects Play/Pause. Previous, Play/Pause, Next, playback mode
and Open are all selectable controls. Space and J/K/R remain direct Music
shortcuts. Active walkie monitoring retains priority for its Space PTT action.
Quick controls exposes Wi-Fi, LoRa, GPS, volume and brightness in that order;
unavailable or changing controls cannot be activated. Both keyboard and touch
requests use the same application control coordinator and audio-focus policy.

The encoder always pages widgets when rotated. Entering slider editing does
not change that rule. The widget footer distinguishes selection from editing.
Only the revealed widget owns its selection state, service subscription and
artwork, all released when it leaves. Selection state is part of the existing
PSRAM widget allocation, without a timer, framebuffer or hidden widget cache.
The container consumes a complete encoder press/release pair even if Open
changes applications while the switch is held.

## Application pages

The shared Pager input adapter repairs a missing initial focus before encoder
dispatch, uses a compact inset focus outline and leaves native LVGL editing
behavior available. Rotation selects controls by default. Press activates a
button or enters an editable/scrollable control; holding the encoder exits
editing when the group contains another control. Reused app groups start in
navigation mode on app entry/rebuild. Chat history also starts in navigation
mode, so Send and Back are reachable immediately.
Now Playing updates changed ID3, artwork and progress in place. Skipping tracks
keeps the same selected transport control, so repeated presses of Next keep
skipping instead of unexpectedly toggling Play/Pause after a page rebuild.
The Now Playing menu contains track information, Add to playlist and Stop;
volume remains available directly with W/S or the Quick controls slider.

Team, Sky Plot, USB, Walkie, SSTV and Extensions also accept W/S to select
controls. Other pages keep their existing semantic shortcuts: for example,
Music uses W/S for volume while Now Playing, Calculator uses letter keys for
scientific operations, and Protocol Probe uses S to start/stop.

Every shared Pager top bar displays `H Help`. H opens help outside text
editors; a focused text editor continues accepting the letter h. Mature
contextual help on Map, Contacts, Network and chat history remains owned by
those pages. Other pages use a shared help popup with navigation instructions
and the relevant app shortcuts. The popup owns keyboard/encoder input and
restores the previous focus on close. Page teardown releases its PSRAM state,
including when an input group was already destroyed. Map preserves the exact
control that opened its help. Two-pane pages retain their selected control on
background refresh and provide Menu/Back even with an empty list.

`tests/site/pager-input-native.test.mjs` uses real LVGL keypad and encoder
input devices with press/release events. It covers launcher edge navigation,
widget buttons/sliders, held encoder ownership, app focus and help restoration,
literal text entry and teardown with help open. The older direct-event preview
helpers remain useful for layout tests but do not prove physical input routing.

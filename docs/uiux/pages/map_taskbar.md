# Map taskbar and upward menus

Status: implemented in the shared Map runtime; native syntax validation passes.
Touch layout and the new navigation integration still require acceptance.

The bar, popup sizing, flat rows and outside-click surface use the
[shared bottom-bar component](../components/bottom_bar.md). Map supplies its
actions and preserves its layer-submenu keyboard behavior.

The bottom bar keeps minus, zoom level and plus at the left. A flexible spacer
places Pos, Help, Team and Menu at the right, in that order. The bar does not
scroll horizontally. Location-picking mode retains its existing Pick/Cancel
actions instead of exposing unrelated tools.

Menu opens upward above its button and contains the contour visibility toggle,
Map Layer, Search, Load navigation and Load track. These invoke the
existing layer, contour, search and file-loading behavior. Navigation selects a
saved navigation KML and activates it for route following; recorded tracks
remain a separate choice.

Map Layer opens a compact second-level picker with OSM, Terrain and Satellite.
It marks the current source and selects the requested source directly. A Back
row returns to the main menu; keyboard Right enters and Left/Escape returns.
Only the current menu level has widgets, keeping the map visible and avoiding
two simultaneous popup trees on small screens. The L shortcut still cycles
layers. Search opens the existing place/POI picker with a shorter menu label.

Team is visible only while the device is in a team with a valid team ID. It is
a separate upward menu next to Menu and directly exposes the existing
member-selection and member-track actions. Member controls are created when
Team opens rather than maintained in a permanently visible side rail. Only
one upward menu is open at a time. Repeating its button or tapping the map
outside dismisses it. Keyboard Escape/Back and the M shortcut remain supported.

Popup width follows its translated labels, capped at the available map width.
Rows use a flat style with a highlight for focus, press and current selection.
Popup height is constrained to the map area above the taskbar, with vertical
scrolling when necessary. Popup controls are destroyed on dismissal. Pos and
Help stay accessible while a popup is open.

Help has a fixed header with a visible Close touch button. Only the help body
scrolls, so Close cannot disappear below the content. The panel is constrained
to the screen width/height. Existing keyboard dismissal remains available.

Acceptance must cover Wio L2 touch at 320 x 240, Pager keyboard input, narrow
layouts, each menu action, switching menus, popup dismissal, team selection,
and opening/scrolling/closing Help without any keyboard event. No physical
device acceptance is claimed by the syntax check.

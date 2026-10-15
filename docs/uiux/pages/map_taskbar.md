# Map taskbar and upward menus

Status: implemented in the shared Map runtime; native syntax validation passes.
Touch layout and the new navigation integration still require acceptance.

The bottom bar keeps minus, zoom level and plus at the left. A flexible spacer
places Pos, Help, Team and Menu at the right, in that order. The bar does not
scroll horizontally. Location-picking mode retains its existing Pick/Cancel
actions instead of exposing unrelated tools.

Menu opens upward above its button and contains the contour visibility toggle,
current map layer, POI search, Load navigation and Load track. These invoke the
existing layer, contour, search and file-loading behavior. Navigation selects a
saved navigation KML and activates it for route following; recorded tracks
remain a separate choice.

Team is a separate upward menu next to Menu. It directly exposes the existing
member-selection and member-track actions. Member controls are created when
Team opens rather than maintained in a permanently visible side rail. Only
one upward menu is open at a time. Repeating its button or tapping the map
outside dismisses it. Keyboard Escape/Back and the M shortcut remain supported.

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

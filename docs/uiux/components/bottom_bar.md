# Shared bottom bar and popup menu

The former filter/list pages use full-width content and a right-aligned bottom
Menu. Tracker, Contacts, Chat and Settings integrate through the existing shared
navigation adapter. Extensions and Network use the same visual components with
their own page focus callbacks. Map uses the same bar and popup implementation
for its Menu and Team controls.

Geocaching already has full-width content with horizontal section tabs. It does
not have a permanent filter column to remove.

## Responsibilities

`ui::components::bottom_bar` provides a bar factory, content-sized buttons,
flat menu-item styling and translated-text measurement. Its `Menu` owns the
temporary outside-click surface and controls upward placement, viewport bounds,
scrolling, dismissal and a single optional submenu. It contains no route,
contact, chat, radio or settings logic.

Pages retain ownership of their existing filter widgets and business state.
`Menu::init` adopts that panel and parks it outside the flex layout while closed;
it does not create duplicate filter buttons. The navigation adapter supplies
activation and focus callbacks, preserving the selected category and item.
The default page focus is the content, so entering a page does not open Menu.

The design uses composition with small function-pointer callbacks. There is no
virtual page hierarchy, global menu registry, `std::function` allocation or
duplicated page-specific popup implementation. `MenuOptions` lets Map retain
its existing keyboard shortcuts and anchor action dispatch while sharing popup
layout and outside-click handling.

## Input and lifetime

- A bar button toggles its menu. Clicking outside closes it.
- Filter activation runs the existing page action and returns focus to content.
- Escape/Back/Left dismiss the current menu level before leaving the page.
- The shared navigation controller retains keyboard and encoder groups.
- Map retains its direct layer picker and independent conditional Team menu.
- Page teardown resets the menu before deleting its root.

Contacts and Chat build their former air-status information only when Info is
selected. Info has a touch Back button and uses the same submenu mechanism.
There are no persistent footer labels or periodic footer hydration on those
pages. Closing Info destroys its transient widgets.

The menu stores widget pointers and callback state, without protocol buffers,
file metadata caches or copied business models. The outside-click surface is
allocated only while a menu is open. Existing filter widgets remain retained;
this refactor does not claim that their memory has been eliminated.

## Verification

`tests/tracker_card_view` includes a real-LVGL bottom-bar contract at 240, 320
and 480 pixel widths. It checks recovered content width, upward placement,
translated-text bounds, exactly-once filter activation, selected-category focus,
lazy Info construction, submenu return, Escape and restored widget counts.

`tests/site/two-pane-layout.test.mjs` exercises all six former two-pane pages
from the production sources compiled to WASM on Pager, T-Deck and Wio L2.
`tracker-page.test.mjs` exercises recording and route cards through the new
category menu. Native preview evidence is not a physical-device capture.

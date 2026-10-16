# Contacts Node Cards

## Goals

Keep the existing left-hand categories for Contacts, Nearby / Announced, and Ignored.
Reuse a single node card on the right, selecting nodes by their full protocol identity.
Broadcast, Discover, Groups, and Team retain their existing behavior.
The card emphasizes the node name, approximate administrative area, and straight-line
distance from the local device. Show the full identity, coordinates, and other geographic
information in the existing Node Info view. Missing coordinates must never appear as
a distance of zero.

## Data and Lifecycle

The browsing index stores only identity bytes, the protocol, a node ID hint, and the
last activity time. MC retains the full 32-byte public key; RT retains the full 16-byte
destination hash. Abbreviated text must not be used for lookup, verification, or sending.
Hydrate only the selected node. Avoid the legacy `getPeerByNodeId` call, which builds
a display cache for the entire directory.
Preserve selection by identity during refresh; a position in a sorted array is not an identity.
Keep the current card when the directory is temporarily unavailable.
Match names, aliases, and identities while enumerating records, without copying complete
search result objects.
Freeze card selection while details or an action menu are open so that the target cannot
change while an action is being performed.

## Presentation

- MT shows a short node ID; MC shows an abbreviated public key; RT shows an abbreviated
  destination hash.
- Prefer a user alias, followed by the name supplied by the protocol. Ellipsize long names.
- Show the complete administrative path with an approximation indicator. Long paths scroll
  horizontally back and forth within a single line; short paths remain stationary.
  Periodic refreshes must not reset the text when the path is unchanged. Administrative
  information inferred from nearby POIs must not be presented as an exact boundary lookup.
- Calculate the straight-line distance over the sphere only when both sets of coordinates
  are valid and within range. Use meters for nearby nodes and kilometers otherwise.
- The last message time is not the position timestamp. Mark a position as stale when its
  known timestamp is more than one hour old.
- MT and MC show only link information that is actually available. RT shows the actual
  identity verification status.
- T-Deck and Wio L2 use 320×240 displays; Pager uses 480×222. Keep the left-hand categories
  consistent with the current specifications for each device.

## Shared Location Component

Contacts calls the common `platform::ui::map_search::submitRegion` and `pollRegion`
ports, also used by Tracker and Geocaching. Their TMAP implementation and
`tmap/administrative_path.h` are independent of all three pages. Shared card
helpers live under `ui/support/card_location.h`; a screen must not own a second
reverse-geocoding algorithm, region database or worker. The full-path scrolling
presentation described above retains the existing local Contacts view changes.

## Administrative Area Lookup

Reuse the PSRAM workspace of the TMAP search task. Administrative area lookup retains
only four nearby POI results and does not allocate the additional 64 KiB read cache used
by name search. Resize the result buffer when switching query modes.
Visit at most 256 candidates per package and enumerate at most 256 packages. Candidate
centers must be within 30 km of the query position.
Use only results that contain an administrative path and are not marked as ambiguous.
Determine their common parent using complete path segments.
Explicitly display an unknown area for older packages without administrative data.
Background lookup must not access LVGL, and results must match the request version.
Distance calculation does not depend on completion of the administrative area lookup.
The node's administrative area can still be displayed when the local device has no position fix.

## Validation and Completion Criteria

Contract tests cover full identities for all three protocols, on-demand reads without
directory enumeration, name and identity search, temporary unavailability, diverging
paths, missing hierarchy levels, and complete UTF-8 path segments.
Validation with real LVGL covers card widget reuse, layout, keyboard / encoder / touch
input, details, and mode switching.
Measure the navigation index, full node projections, widget count, and peak memory use
at different node counts. Device memory figures must come from actual measurements;
host-side `sizeof` values are not a substitute.
Accept a PlatformIO build result only after the process has explicitly exited.

# Contacts Node Cards

Contacts, Nearby / Announced and Ignored reuse one node card with previous/next
navigation. Broadcast, Discover, Groups and Team retain their existing actions.
The card emphasizes the node name, approximate administrative area and distance;
full identities and coordinates remain available in Node Info.

## Data and lifecycle

The browsing index stores full protocol identity, a node ID hint and last activity
time. MeshCore retains its full 32-byte public key and Reticulum retains its full
16-byte destination hash. Abbreviated display text never becomes a lookup or
action identity. Only the selected peer is hydrated through `ContactService::readPeer`;
search and enumeration do not copy complete directory projections.

Keep selection by identity when the directory refreshes and retain the current
card during temporary unavailability. Freeze its target while details or an
action menu are open. Keyboard, encoder, touch and existing page shortcuts use
the same card selection.

## Location presentation

The card calls the common `platform::ui::map_search::submitRegion` and `pollRegion`
ports, also used by Tracker and Geocaching. Their TMAP implementation and
`tmap/administrative_path.h` are independent of all three pages. Do not put a
reverse-geocoding algorithm or region database in a screen.

Mark an inferred area with `~`, retain complete hierarchy segments and avoid
splitting UTF-8 text. The compact card uses the final two available segments.
An approximate area inferred from nearby POIs is not an exact administrative
boundary lookup. Display an unknown area when map data lacks useful metadata.

Distance needs valid node and local coordinates. Display meters for nearby
nodes and kilometers otherwise; missing positions must not appear as zero
distance. Region lookup does not require the local device to have a GNSS fix.
Position age comes from its timestamp, not from the last message time. Mark a
known position older than one hour as stale.

## Resource use and verification

Reuse the shared TMAP worker and its PSRAM workspace. Region lookup keeps four
result projections and avoids the additional 64 KiB name-search cache. It does
not create a worker for each page. Background work must not access LVGL, and
results must match the request version.

The peer-directory contract checks full identities, on-demand reads, filtering,
search and unavailable-directory behavior. The real-LVGL view test checks layout,
constant widget count during repeated card updates and button events at 320×240
and 480×222. These are host checks; device memory and physical interaction need
their own acceptance evidence.

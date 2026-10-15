# Tracker item cards

Tracker uses one reusable card for Tracks, Routes and Navigation. It retains the
filename index but reads display metadata only for the selected file.

The title uses the KML Document name, falling back to the first named Placemark.
XML entities, namespace prefixes and CDATA are supported. Empty names fall back
to the filename without its extension. Installed shared assets use
`Route <first eight revision characters>` when no title is available; their full
immutable revision is not used as the card heading. Storage names, package IDs,
signature checks and load/delete targets remain unchanged.

Below the title, the card shows distance to the first recorded position, the
approximate administrative region, then file type and status. Distance is not
the total route length. The existing region worker is reused. Routes and
Navigation hide the redundant filename status above the card; recording status
remains visible in Tracks.

Previous, count, next and a localized Actions button form the card footer.
Clicking the card also opens its existing action menu. Touch gestures and
keyboard selection remain available. The title has at most two lines; the
region can scroll within its row.

Title extraction shares the current selected-file read (at most 64 KiB) with
first-position extraction. Its bounded XML workspace is transient PSRAM and is
freed after the read. No per-route metadata cache or geometry collection is
introduced. Switching files reuses the same widgets.

Verification: `tests/tracker_card_view` renders the production card component
against LVGL at 320×240, 480×222 and 240×320, checks control bounds and Actions,
and exercises title decoding, UTF-8 truncation, hash fallback and combined
title/coordinate reads. The surrounding native page is a test scaffold, not a
physical device capture. Physical acceptance of this revision is pending.

# Release documentation screenshot sources

These files are copies of actual interface captures, not generated concept art.
Their captions distinguish sample data from hardware acceptance. Final release
review should refresh captures if the corresponding UI changes.

| File | Source | Evidence boundary |
| --- | --- | --- |
| `web-config-percent.png` | `tests/site/device-page.browser.mjs --normal-flow`, output `images/config-percent-zh-Hans.png` in the local Web planning folder | Actual browser controls; simulated USB values. |
| `web-geocaching-maintenance.png` | `tests/site/geocaching-network-maintenance.browser.mjs`, output `images/geocaching-real-network-archive.png` | Actual browser/native directory path; simulated USB author and sample cache. |
| `web-track-elevation.png` | `tests/site/track-workspace.browser.mjs`, output `.codex-build/hostlink-codec/track-elevation-own.png` | Actual browser controls and sample KML; no real hiking or hardware assertion. |
| `web-track-country-search.png` | Local `live-track-page.mjs` using the review worktree's built page and actual public WSS/Reticulum directory, 2026-10-14 | Country pagination, verified Austria boundary and empty regional query; no USB author, public write or published-website assertion. |
| `center-map-cache.png` | Trail Mate Center repository `assets/QQ20260515-103200.png` | Existing desktop screenshot; separate Center release, not a firmware acceptance capture. |
| `center-propagation.png` | Trail Mate Center repository `assets/QQ_1778812190407.png` | Existing desktop screenshot; no completed radio simulation claimed. |
| `firmware-geocaching-card-native.png` | Production Geocaching LVGL page rendered by `modules/ui_shared/tests/geocaching/test_page.cpp`, 320×240, 2026-10-14 | Reused card with distance and a synthetic approximate administrative area; not a geographic-accuracy measurement, camera capture or real-device publication acceptance. |
| `firmware-tracker-card-native.png` | Actual Wio LVGL native preview from CI run `38552778708`, source `5ea6e635`, 320×240, 2026-10-14 | Sample KML and simulated GPS. Zero distance is the demo fix matching the sample start; the preview has no TMAP administrative dataset, so the region is explicitly unknown. Not a device camera capture or real hiking record. |

Firmware illustrations already live under `docs/images/`, including map,
messaging, team and route-preview captures. The final 1.0.0 screenshot set still
includes native Geocaching/Tracker card captures. Physical card interaction and
TMAP geographic accuracy still require hardware acceptance; native captures do
not establish those results.

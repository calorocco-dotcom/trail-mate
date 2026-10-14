# Release documentation screenshot sources

These files are copies of actual interface captures, not generated concept art.
Their captions distinguish sample data from hardware acceptance. Final release
review should refresh captures if the corresponding UI changes.

| File | Source | Evidence boundary |
| --- | --- | --- |
| `web-config-percent.png` | `tests/site/device-page.browser.mjs --normal-flow`, output `images/config-percent-zh-Hans.png` in the local Web planning folder | Actual browser controls; simulated USB values. |
| `web-geocaching-maintenance.png` | `tests/site/geocaching-network-maintenance.browser.mjs`, output `images/geocaching-real-network-archive.png` | Actual browser/native directory path; simulated USB author and sample cache. |
| `web-track-elevation.png` | `tests/site/track-workspace.browser.mjs`, output `.codex-build/hostlink-codec/track-elevation-own.png` | Actual browser controls and sample KML; no real hiking or hardware assertion. |
| `center-map-cache.png` | Trail Mate Center repository `assets/QQ20260515-103200.png` | Existing desktop screenshot; separate Center release, not a firmware acceptance capture. |
| `center-propagation.png` | Trail Mate Center repository `assets/QQ_1778812190407.png` | Existing desktop screenshot; no completed radio simulation claimed. |

Firmware illustrations already live under `docs/images/`, including map,
messaging, team and route-preview captures. The final 1.0.0 screenshot set still
needs current firmware Geocaching/Tracker and full Web overview captures; do not
present this initial set as complete coverage.

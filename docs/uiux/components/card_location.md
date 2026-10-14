# Shared Card Location Presentation

Contacts, Tracker and Geocaching must consume the same approximate administrative
location service. No page owns a separate reverse-geocoding algorithm or region
database.

## Boundaries

- `core_tmap/tmap/administrative_path.h` reduces administrative paths to complete
  common hierarchy segments without splitting UTF-8 or inventing names.
- `platform::ui::map_search::submitRegion` and `pollRegion` expose the shared,
  asynchronous coordinate-to-administrative-area operation. The ESP adapter
  reads nearby TMAP POIs, filters ambiguous paths and returns their common area.
  Contacts already consumes this API; Tracker and Geocaching use the same API.
- `ui/support/card_location.h` formats the current item's approximate area and
  straight-line distance. It depends on platform ports, not on any screen.
- `ui/support/card_file_location.h` reads the current Tracker file's first point
  for presentation. It does not infer administrative names or load all routes.

## User-facing behavior

The primary card shows name, distance, approximate administrative area and item
state. Coordinates belong in details or editing, not the main card. Mark inferred
areas with `~`; these are estimates from nearby POIs, not a polygon containment
claim. Display unknown area when maps lack usable administrative metadata.

Distance requires a valid item position and a valid local GNSS fix. Use meters
below one kilometre and kilometres above it. Missing positions must not display
zero distance. Region lookup can work without a local GNSS fix. Refresh distance
when the local fix changes and preserve unchanged scrolling region text.

## Resource ownership

Reuse the existing TMAP worker and PSRAM workspace. Region mode keeps four result
projections and avoids the 64 KiB name-search read cache. Do not start one worker
per page or scan every item. Card location state lives in PSRAM and is released
when its owning page exits. The current-file parser also uses transient PSRAM,
stops after the first point and reads at most 64 KiB for a card preview; unavailable
preview position does not invalidate the file or prevent opening its details.

The shared implementation must remain independent of Contacts, Tracker and
Geocaching. New pages should call the platform service and reuse presentation
helpers rather than importing another page's implementation.

## Verification

The real-LVGL Geocaching tests cover one-item hydration, fixed card widgets,
approximate area without a local fix, distance updates and page cleanup at three
screen sizes. Their region backend is deterministic synthetic data, not evidence
of geographic accuracy on a physical device. Current-file tests exercise the
production KML, GPX and CSV first-point reader. Physical TMAP lookup and Tracker
page acceptance remain separate release checks.

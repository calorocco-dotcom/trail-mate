# Offline navigation and Map controls

This guide describes the implemented 1.0.0 candidate. The latest navigation,
Map-menu and Tracker-card changes still require physical-device acceptance;
their presence in this guide is not a stable-release announcement.

## Prepare routing data

Rendering a map and calculating a path require different data. An OSM, terrain
or satellite package may display normally while containing no routing graph.
Use Trail Mate Center's detailed OSM export with an embedded foot-routing graph,
and copy its generated SD tree directly, including
`/maps/tmap/osm/<region>-osm-z<min>-<max>.tmap` and font resources.
London and Kunming detailed packages carry navigation; world/country overview
packages are not a promise of navigable road coverage.
Keep the relevant map and POI packages installed for visual context and place
search. The terrain and satellite layers show only the zoom levels present in
their respective packages.

The current calculation runtime is available on supported PSRAM-capable Arduino
ESP and ESP-IDF targets. The Nav menu choice is hidden when the runtime
is unavailable. This integration currently calculates foot routes within one
installed routing package. It does not offer car/bicycle profiles, runtime
conditional-rule evaluation, or route stitching across separate graphs. Prepare
a connected regional graph in Center when a trip needs wider coverage.

## Create an origin-to-destination plan

1. Open **Tracker → Menu → Nav**, then **New navigation**.
2. Keep **Current location** as the origin, or choose another origin using the
   existing POI search. A current-location origin needs a valid GNSS fix when
   calculation starts.
3. Select the destination through the same POI picker. Choose a search result;
   typing a name alone does not select its coordinates.
4. Choose **Calculate**. The page reports progress and allows cancellation.
5. When a route is ready, review the calculated distance, then choose
   **Save and preview** to inspect the path on the map.

Missing routing data, endpoints outside coverage, no available path, and storage
or memory admission failures are separate outcomes. An unsuccessful calculation
does not become a straight line labelled as a navigable path.

Saved plans are self-contained `.tmroute` files in `/routes/navigation`. They
retain computed geometry, cumulative distances and file indexes. Filenames use
the endpoint names; the archive does not store the original POI records or
semantic maneuvers. Reopening does not recalculate from the current position.
The geometric route end, snapped within 50 m during planning, is the destination
used for following/replanning. Saved navigation and KML trail publishing are
separate workflows; saving navigation does not publish a shared trail.

## Follow a saved plan

Choose a saved Navigation card in Tracker, open **Actions**, and use its preview
or load action. Alternatively, open **Map → Menu → Navigation** to load a plan.
An active plan shows progress/remaining distance and geometric left/right/U-turn
cues. The reader queries the visible viewport; it does not load the whole route.
Geometric cues have no road names or intersection semantics.

The Navigation dialog offers **Replan from here**, **Reload route** and
**Automatic replan**, with a fixed **Stop navigation** footer. A running replan
shows **Cancel replan**; failure offers **Retry replan** and keeps the saved route.
A candidate is opened before the active configuration is replaced. Automatic
replanning requires sustained deviation and a fresh fix, permits one attempt per
continuous deviation and waits at least 30 seconds between automatic attempts.

Arrival requires prior progress, proximity to the geometric endpoint and five
seconds of fresh observations. It clears the active route, releases the session's
GPS lease and leaves a completed view with **Finish navigation**. Other features
may still hold their own GPS leases. Stop/Finish does not delete saved plans.
Following runs while Map is open; leaving releases the session and reopening Map
resumes the saved active path. Background guidance from other pages is not supplied.

Tracker's bottom **Menu** keeps **Tracks**, **Routes** and **Nav** separate. Tracks contains
recordings, Routes contains ordinary route files, and Navigation contains saved
calculated plans. Each mode reuses a single card with previous/next controls.
Route names come from KML when available; an internal revision filename is not
used as the main title. Cards show approximate administrative area and distance
to the first recorded position. That card distance is not total route length;
use the calculated result or route preview for route length.

## Use the Map taskbar

The bottom bar keeps the zoom controls at the left. **Pos**, **Help**, **Team**
and **Menu** appear at the right, in that order. **Team** is visible only while
the device has joined a team with a valid team ID.

**Menu** opens upward and contains contour visibility, map-layer selection,
**Search**, **Navigation** and **Tracks** management. **Map Layer** opens a second
level with direct **OSM**, **Terrain** and **Satellite** choices, marking the
current source. **Team** opens its own upward
menu for member selection and member-track actions, making team operations
accessible without entering the general tool menu. Only one menu is open at a
time. Tap its button again or tap the map outside it to dismiss it. Long menus
scroll within the space above the taskbar.

**Help** has a fixed, visible **Close** button. Its help text scrolls separately,
so Wio L2 users can dismiss the panel without a keyboard. Keyboard Help and
Escape/Back dismissal remain available on devices that have keys.

POI label budgets are approximately 40 percent higher than the previous policy,
while retaining the existing overlap checks and placement buffers. Actual
visible counts depend on zoom, package contents and available screen space.

## Storage and resource use

Search arrays, reader workspaces and route UI owners explicitly request PSRAM on
ESP; they do not fall back to internal RAM. Planning streams legs to an exclusive
SD spool, releases search arrays, then exports `.tmroute`. Planning buffers and
the task are released after completion or cancellation. Following keeps a fixed
reader and a 128-segment screen snapshot. Dense overview views request zooming
in instead of silently truncating geometry. Stale GPS, SD loss, corruption and
memory/storage limits have visible states.

Center's real London route is 20,778.62 m, with 1,171 geometry points and a
43,776-byte saved archive. A 32-bit host run of the actual production worker and
SD adapters measured a 2,143,140-byte external allocation payload peak and about
1.54 GB of map/spool reads. Actual LVGL replay matched all 1,170 segment endpoints;
the eight fitting tested viewports were pixel-identical to a full-geometry host
oracle, with zero route reads during drawing. The Map owner measured 1,576 B and
its separate reader/page/cursor payload 2,160 B. Four coarser views requested zoom
in; all tested z16/z18 views fit the fixed snapshot.

These measurements exclude real driver/allocator overhead, task stack, display,
other features and concurrent readers. They do not establish physical PSRAM/RAM
high-water, MCU calculation time, SD latency or energy use. A device with a few
KiB of free RAM cannot run the complete planner. Reproducible tests and raw
evidence are maintained in `docs/engineering/native-navigation.md` and
`native-navigation-application-evidence.json` in the firmware repository.

Navigation calculation and following do not require an Internet account or a
Reticulum directory. Publishing a trail is a separate operation using the
connected device's author identity and the computer's network.

See [[21. Trails and Decentralized Sharing]], [[5. Offline Maps]], and
[[6.2 TMAP Maps and Terrain]] for sharing and map preparation.

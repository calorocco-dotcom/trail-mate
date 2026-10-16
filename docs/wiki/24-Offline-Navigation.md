# Offline navigation and Map controls

This guide describes the implemented 1.0.0 candidate. The latest navigation,
Map-menu and Tracker-card changes still require physical-device acceptance;
their presence in this guide is not a stable-release announcement.

## Prepare routing data

Rendering a map and calculating a path require different data. An OSM, terrain
or satellite package may display normally while containing no routing graph.
Export a compatible foot-routing `navigation.tmap` with Trail Mate Center and
copy it to `/maps/navigation` or `/maps/tmap/navigation` on the device's SD card.
The firmware also checks the existing `/maps/tmap` packages for routing data.
Keep the relevant map and POI packages installed for visual context and place
search. The terrain and satellite layers show only the zoom levels present in
their respective packages.

The current calculation runtime is available on supported PSRAM-capable Arduino
ESP targets, including Wio L2. The Nav menu choice is hidden when the runtime
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

Saved plans are KML files in `/routes/navigation`. They retain their chosen
endpoints and computed geometry. Reopening a saved plan does not recalculate its
origin from the current position. Plans remain local until the user separately
imports and publishes them through a sharing workflow.

## Follow a saved plan

Choose a saved Navigation card in Tracker, open **Actions**, and use its preview
or load action. Alternatively, open the Map menu and choose **Load navigation**.
Loading activates the route overlay and the existing route-following displays
for current position, route progress, remaining distance, deviation and arrival.
Stopping navigation does not delete the saved plan.

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
**Search**, **Load navigation** and **Load track**. **Map Layer** opens a second
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

Calculation workspaces and route output are temporary PSRAM allocations on
supported ESP targets. Saving streams the calculated geometry into KML. Planning
buffers and the calculation task are released after completion or cancellation;
there is no second road-graph cache kept alive while Tracker is idle.

Navigation calculation and following do not require an Internet account or a
Reticulum directory. Publishing a trail is a separate operation using the
connected device's author identity and the computer's network.

See [[21. Trails and Decentralized Sharing]], [[5. Offline Maps]], and
[[6.2 TMAP Maps and Terrain]] for sharing and map preparation.

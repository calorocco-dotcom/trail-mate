# Map Annotation Policy: Text-Free Base Maps at All Zoom Levels

Status: approved by the user on 2026-09-15 and under implementation. The delivered
verification package still contains labels at z1–15 and uses text-free tiles
with point POIs at z16–18. This specification is not a statement that the next
firmware implementation is complete.

## Requirements and object boundaries

The user accepts text-free tiles at every level from z1 to z18, provided the
annotation rules are reasonable. Road names are essential at z16–18. Dense
icons, sparse names and labels appearing before disappearing must be addressed
together.

- Geographic data: roads, settlements, regions, facilities and POIs present in
  the source. An object's existence does not require display at every zoom.
- Map annotations: names and screen presentation of geographic objects,
  including place/region names, road names, and point-POI names/icons. Roads
  must not be reduced to one ordinary POI at the road's midpoint.
- Display eligibility: determined by category, zoom and user preset, rather
  than whether a memory allocation succeeds.
- Layout result: annotations that are readable and stable on the given screen.
  A name that does not fit is not missing data; do not delete its source or
  mark the base map as missing.
- Resource limits: bound candidate, cache and drawing costs rather than serving
  as category filters.

Reuse the shared map viewport, coordinate projection, background spatial-tile
reads, event/generation mechanism and resource lifecycle. Firmware work is on
`feat/wio-tracker-l2`; do not create a page-private loading pipeline.

## Default zoom-level eligibility

This table defines the initial style. Names must come from data. Administrative
level, road class, population or importance determine hierarchy; the table does
not guarantee complete coverage from every source. Eligible annotations must
still pass screen layout.

| z | Places, regions and point POIs | Road annotations |
| --- | --- | --- |
| 1 | Continents and major countries; a regional package may retain one place name locating its selected area | None |
| 2 | Countries and major capitals | None |
| 3 | Countries, capitals and a few important cities | None |
| 4 | Province/state regions, their capitals and important cities | None |
| 5 | Major cities and important large lakes/mountain ranges | None |
| 6 | Cities, major county-level cities and large natural landmarks | None |
| 7 | County-level settlements and important towns | A few important motorway/national-road numbers |
| 8 | Towns and important townships; a few names locating large scenic areas | Sparse motorway/national-road numbers |
| 9 | Townships, large villages, important peaks and scenic areas | Major trunk-road numbers |
| 10 | Villages and important outdoor destinations, such as trailheads, camps and shelters | Important trunk-road numbers or short names |
| 11 | Villages, important natural landmarks and major outdoor service facilities | Trunk-road names/numbers |
| 12 | Neighbourhoods/districts, major parks, hospitals, major parking and outdoor facilities | Urban arterial and major-road names |
| 13 | Neighbourhoods, parks, attractions, transport hubs and major public facilities | Primary/secondary road names |
| 14 | Blocks/villages, parks, commonly used public facilities and outdoor service points | Primary/secondary roads and important side-road names |
| 15 | Blocks and important buildings/destinations; practical POIs such as water, toilets and parking | Side-road and major residential-street names |
| 16 | Relevant nearby named facilities, attractions and outdoor POIs | Arterial roads and ordinary named streets must be eligible; prioritize names of visible roads |
| 17 | Finer nearby facilities, entrances, small parks and named buildings/destinations | Ordinary streets, paths and named pedestrian passages |
| 18 | Facilities and entrances at doorway/intersection scale, named buildings and small landmarks | Named service roads, side roads, pedestrian paths and other detailed roads |

The user position, current route and selected destination use the existing
navigation overlays, with priority independent of ordinary geographic labels.
Ordinary hospitals/emergency facilities must not displace country or city names
at country scale solely because of their category.

A regional PBF is not a global place-name dataset. Country/capital display at
z1–3 requires an appropriate source. A Yunnan-only PBF may show a real place name
locating the selected area, but must not imply global coverage. If a global
overview is needed, Center must prepare lightweight place-name data separately,
without relying on third-party finished raster tiles.

## Addressing dense icons and sparse text

1. Lay out road and region names directly as text without reserving an icon first.
2. Treat a named POI's small icon and name as one combined layout candidate.
   Icons for unselected candidates must not block other candidates' text.
3. Unnamed category icons, such as toilets and parking, supplement practical
   information rather than filling the screen unconditionally. For dense points
   of one category, select useful representatives or use a count-bearing cluster
   when necessary. Do not indiscriminately merge different semantic categories.
4. Ellipsize long names safely at UTF-8 boundaries, retaining a recognizable
   prefix. Do not discard the entire name solely for exceeding a fixed width.
   Do not enlarge icons or use scrolling text.
5. Resolve collisions before choosing the displayed set. Once a rejected
   candidate releases its position, allow later suitable candidates to fill it.
   Remove the process that selects the first 48 entries, rejects collisions and
   never fills the resulting gaps.
6. Give roads, places and POIs initial display budgets, allowing other categories
   to use unspent capacity. Avoid 48 icons occupying space while only a few names
   remain.
7. Initially try 10–12 px icons and 14–16 px regular text. These parameters require
   physical-device confirmation. The user's additional requirement is that all
   road, place and POI text have transparent backgrounds: no white/off-white
   backing panels and no white outlines or halos. Readability depends on font
   size, weight, color and collision avoidance.

For Pager at 480×222, initial layout targets are approximately 4–10 useful labels
at low zoom, 10–18 at middle zoom and 20–30 total at high zoom. At high zoom,
initially reserve approximately 6–10 opportunities for road names, with the
remainder for places and POIs. These are debugging targets when data and space
permit, not promised or mandatory counts. The actual map viewport is smaller
than the screen; adjust to usable area and measured device timings.

The user's density adjustment on 2026-10-15 raises the zoom-band named-label
budgets from 8/16/24/32 to 11/22/34/45 (approximately 40%). POI reservation rises
from 12 to 17 while road/place reservations and collision avoidance stay intact.
The existing 48-placement frame, 192-candidate cap, snapshot buffers and draw
objects are not enlarged. Actual visible counts still depend on source coverage
and available non-overlapping space; this is not a guarantee of 40% more labels
in every viewport. The runtime and native replay use the same budget function.

## Road-name data and layout

- Center retains road identity, name/number, road class, zoom eligibility,
  simplified segments and multiple candidate positions.
- Merge same-name segments only when topologically continuous and semantically
  consistent. Do not merge same-name roads from different regions.
- Cover multiple parts of long roads with candidate positions, so a visible
  segment can show its name while the midpoint is outside the viewport.
- The device processes only simplified candidates near the viewport. Position
  text to match the road while keeping a readable screen orientation; do not
  inherit rotation of the entire base map.
- Deduplicate names of the same road segment and control repeated labels'
  screen spacing along roads. Text must fit entirely inside the usable viewport.
- Express the importance of z16–18 road names through eligibility and layout
  budgets, not hardcoded exceptions for a few named roads.

## Temporal stability and failure semantics

- Preserve the previous selection through stable geographic object IDs.
  Prefer retaining labels that remain eligible, visible and free of substantive
  conflicts.
- Replace an existing label only when a new candidate is substantially more
  important or enough usable space becomes available. A one- or two-pixel
  difference in distance from the center must not cause repeated swapping.
- Merge neighbouring tiles as they arrive asynchronously. Gradual loading of
  a stationary view must not continually flash or remove existing names.
- Recompute layout only when the viewport/zoom, relevant data, font metrics or
  style changes. Ordinary redraws reuse the existing result.
- On PSRAM allocation or temporary read failure, retain usable old results and
  defer updates only while they still match the current view/zoom. An error
  must not mean that there are no annotations. Do not retain content for an old
  view/zoom indefinitely merely to avoid flicker.

## Center, packages and memory

- Change the default to text-free base maps plus map annotations. Configure
  places, road names and point POIs separately; disabling facility POIs must not
  also disable road names.
- Provide outdoor/urban presets and minimum/maximum zoom for each group.
  Defaults cover all 18 levels; users should not manage 18 times every category
  of switches individually.
- Urban presets may include public transport, schools, parks and named
  destinations. Presets/category choices control shops, food and similar POIs.
  Do not claim the current nine-category extractor already provides all of them.
- Extend the existing spatial-index payload so one annotation pipeline can
  distinguish places, roads and point POIs. Verify the disk version/migration
  with the implementation. Do not make old POI files and new annotation files
  simultaneous authoritative sources.
- Apply the same zoom/style rules to Center preview, export and device
  consumption. An online preview background must not stand in for the final
  local text-free base map.
- Font resources must cover every exported annotation name, including roads
  and places, using the existing font-pack mechanism. The SD delivery package
  must include missing required resources instead of depending on a manually
  patched Kunming-specific character list.
- Keep PBF geometry processing, simplification, candidate generation and zoom
  classification in Center. The device does not parse the entire area's road
  network or retain large objects/full road tables.
- Use PSRAM for caches, candidates and layout buffers on demand with hard
  budgets. Reuse background-read scratch storage, retain a single drawing layer,
  and do not add icon/text widget pools or another full-screen canvas.
- Separate memory budgets from display density: improve layout first, then set
  candidate limits using actual device costs. Raising 48/20 alone is not a fix.

## Implementation order and acceptance

1. Complete the path from PBF road/region names to the unified annotation index
   and shared map drawing layer.
2. Fix combined text layout, long-name ellipsis, gap filling and stable selection;
   define what is retained or cleared after failure.
3. Integrate zoom rules, Center presets/preview and font-resource packaging.
4. Only after the annotation pipeline has the required coverage, generate the
   next z1–18 text-free verification package. Do not remove low-zoom text before
   dynamic annotations can replace it.
5. Verify z1–18 on the same Kunming area, particularly ordinary street names at
   z16–18, long roads crossing tiles/viewport edges, same-road deduplication,
   dense facilities and long names.
6. Verify gradual loading of a stationary map, small location jitter, panning,
   zooming, page entry/exit, delayed fonts and constrained memory. Record
   candidate counts, actual road/place/POI label counts and rejection reasons.
7. Measure native RAM/PSRAM high-water marks, layout/drawing times and task stacks.
   Host tests cannot replace these measurements on physical hardware.

The user approved the zoom rules and improvement direction, and explicitly
prohibited text backing panels and white outlines. Font sizes and density still
need device validation. This document does not state that the next implementation
has been delivered.

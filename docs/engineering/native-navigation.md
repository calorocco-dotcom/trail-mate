# Native saved-route navigation

Trail Mate consumes the released TMAP SDK 0.2.0 and stores calculated foot routes
as self-contained `.tmroute` files. Tracker creates and previews them; Map reads
the visible geometry, follows fresh GPS fixes and owns the navigation session.
The existing KML following path remains supported. This document distinguishes
host acceptance, target compilation and physical-device measurements.

## Required lifecycle

Select current/searched endpoints; calculate with an explicit external-memory
budget; write legs directly to an exclusive SD spool; release search storage;
export and finalize a self-contained `.tmroute`; sync and publish; list, preview
and activate it; read only the current viewport; match fresh GPS fixes; show the
next turn and distance; replan after confirmed deviation; finish or cancel the
session and release resources. Loading errors, missing GPS/SD, ambiguous matches,
memory/storage budgets, cancellation and failed replanning need visible UI states.

## Implementation and acceptance matrix

| Requirement | Implementation and current evidence | Verification limit |
| --- | --- | --- |
| Sink-based search and phased export | Actual production worker, `ArchiveCalculation`, SD map/archive adapters; restricted-turn fixture and Center London 20.779 km pass | Host SD/RTOS ports do not establish device latency |
| Independent route reads | Map's `TmRouteMap` uses the renderer's projection and viewport; fixed snapshot; LVGL draw callback performs no file I/O | Host display adapter, no physical screen capture |
| External-memory policy | Explicit external owners, `RequireExternal` and PSRAM-only file handles; refusal and complete release tests pass | Whole-firmware concurrent RAM/PSRAM peaks remain unmeasured |
| Temporary file ownership | Exclusive creation in Arduino and IDF; partial transfer/collision/media-session tests; cancellation survives a denied UI mutex | Sudden power-loss durability is not established |
| Named saved routes | Actual worker save/list/reopen/full validation/remove test; Tracker native thumbnail and Map readers | UI creation dialog uses host platform fakes in LVGL tests |
| Progress and turn lookahead | 1,170 real London segment endpoints match; 299 geometry-turn hints; final cue is published atomically | Geometry hints have no street names or intersection semantics |
| Ambiguity, stale fixes and deviation | Stale GPS, ambiguity fixture, off-route dwell and SD-loss cleanup tested; distinct visible Map states | Synthetic fixes, not a recorded physical GNSS walk |
| Replanning | Automatic/manual policy, cooldown, cancel/retry and failed-candidate retention; new archive validated before config/path swap | Session policy and actual worker tested separately on host |
| Arrival and end | Prior progress, endpoint proximity and five-second fresh dwell; session releases its GPS lease; config is cleared and Finish remains visible | Other GPS consumers may retain their own leases |
| UI controls | Actual LVGL dialog tests: dynamic Cancel/Retry/Finish, keyboard focus recovery and fixed touch Stop footer | No claim of physical input acceptance |
| Interactive route preview | Actual Tracker model/gesture/control/key handlers tested with LVGL: drag, zoom, Fit, retained view and image-pin selection; native detail uses `TmRouteMap` | Tile loading and physical input are separate acceptance steps |
| Reader contention and dense views | Actual Arduino LVGL filesystem callbacks and archive reader tolerate 11 injected Busy events; a 10,000-point dense view recovers after zoom without increasing reader storage | Host fault injection does not reproduce physical SD timing |

## Using saved navigation

1. Install Center's map and font output using its SD directory structure. A
   detailed OSM package must contain a foot-routing graph; raster coverage by
   itself cannot calculate a route.
2. Open **Tracker → Menu → Nav → New**. Choose a searched destination and either
   the current fresh GNSS position or a searched origin. Calculate, then choose
   **Save and preview**. The saved file lives in `/routes/navigation`.
   Drag the preview map to move it; use **+**, **−** and **Fit** to zoom or restore
   the route overview. Keyboard devices including Pager use **W/A/S/D** to pan,
   **Q/E** to zoom out/in, and **F** to fit. Arrow and **+ / −** aliases remain
   available. Press **H** for Preview Help; **W/S** scrolls the help. When the
   image strip is shown, **W/S** selects images; hide it with **P** to pan in
   all directions. **G** downloads images and **V** toggles the profile.
   Changing map layers or image selection preserves the chosen view.
3. Load the selected saved plan as the active route and open Map. The filename
   identifies the saved plan; the route is reopened after leaving/reentering Map
   or restarting the application. A preview alone does not activate following.
4. **Map → Menu → Navigation** offers **Replan from here**, **Reload route**,
   **Automatic replan** and **Stop navigation**. During a job the replan action
   becomes **Cancel replan**; failure offers **Retry replan**. A candidate is
   opened successfully before replacing the active path. Cancellation/failure
   leaves the previous saved plan available.
5. After confirmed arrival, active-route configuration is cleared, the completed
   view remains available and the fixed footer becomes **Finish navigation**.
   Stop/Finish closes the session without deleting saved routes. Delete is a
   separate Tracker file action.

The current navigation session runs while Map is open. Leaving Map releases its
navigation reader/session and power lease; the saved active path resumes on
reentry. Background spoken guidance or guidance from other pages is not provided.
Planning is offered on PSRAM-capable ESP Arduino and ESP-IDF ports. Other shared
UI targets can read saved archives but do not advertise an available planner.

## Resource policy

`ArchiveCalculation` defaults to 65,536 search labels and a 2 MiB array budget,
200,000 expansions, 65,536 maximum spooled legs and an 8 MiB archive budget.
The arrays are constructed in bounded slices. ALT is selected when landmark
sections are present; otherwise bounded heap Dijkstra is used without allocating
unused landmark storage. Invalid landmark metadata is not silently ignored.
Search arrays/cursor are released before the archive workspace/exporter are
created. The application owns the files and their lifetime throughout each job.

The new `SavedRoute` has no map/graph dependency. One reader/page workspace is
shared by two small cursors, so a viewport query can interleave with matching and
lookahead. No complete geometry or route-sized RAM index is retained. Queries
have work/node/segment bounds; budget exhaustion must not be shown as a complete
route. Geometry turns are directional hints, not street-name or surveyed-junction
instructions. Repeated/stale fixes do not establish arrival or deviation dwell.

Map keeps 128 clipped screen segments (1,024 B) rather than all route geometry.
Old-camera geometry is cleared immediately on pan/zoom; a complete query is
published together. If more than 128 visible segments are needed, the UI asks
the user to zoom in instead of showing a silently truncated line. Tracker's
96-point overview provides initial bounds and summary metrics; native preview
detail uses another instance of the same bounded reader. Each preview retains
20 B of scalar camera state and one fixed snapshot rather than a full polyline.
The preview reader and its archive workspace require external memory on ESP;
there is no fallback to internal RAM for those owners. Closing the preview
deletes its poll timer and releases its reader. LVGL controls, map tiles and
allocator overhead are additional costs, not part of the reader payload.
Queries run in bounded poll slices; a single synchronous SD call can still take
longer than the nominal 4 ms poll budget on actual hardware.

Fresh fixes are at most five seconds old. Core matching accepts reported accuracy
up to 30 m; the current application GPS port supplies no accuracy estimate, so
Map uses the default zero uncertainty and still enforces age. Reliable following
is within 15 m, deviation requires more than 35 m for five seconds, and arrival
requires being within 10 m of the geometric endpoint with at most 10 m remaining
for five seconds after earlier progress. Endpoints are snapped within the
producer's 50 m bound. Original POI names/requested coordinates and semantic
maneuvers are not stored by the archive; the saved geometric end is the replan
destination. Automatic replan is limited to one attempt per continuous deviation
and a minimum 30-second interval; manual retry remains available.

`SdRouteFile` uses `SdRuntimeMemoryPolicy::PsramOnly`, checks the medium session,
and retries partial reads/writes from the original offset. New files use exclusive
creation. Cleanup must only remove files created by the current job. Existing
saved routes are not overwritten during calculation or unsuccessful replanning.

## Current real London pipeline evidence

Input: Center's `maps/tmap/osm/london-osm-z13-17.tmap`, from the SD delivery
manifest, SHA-256 `122b58d9ae7eb124f5f67d23dba204deb53506fd3ede17cdafb2022a814e75a1`.
Coordinate request: `(51.5009000, -0.1245000)` to `(51.4036000, -0.3377000)`,
foot mode, 50 m snapping, ALT and default pipeline limits.

| Metric | Measured 32-bit MinGW host result |
| --- | ---: |
| Geometry length | 20,778.62 m |
| Clipped legs / geometry points | 550 / 1,171 |
| Provider peak during search | 2,108,244 B |
| Provider live bytes after search release | 10,024 B |
| Provider peak during export | 11,800 B |
| Live allocations after pipeline completion | 0 B |
| Saved route file | 43,776 B |
| Search logical map reads | 2,229,930,944 B |

Normal and every-seventh-operation Busy runs produce identical complete files.
The current [machine-readable pipeline evidence](native-navigation-pipeline-evidence.json)
records both runs, the saved file hash and the compiler frame maximum.
Each run closes the map, spool and output before reopening and fully validating
the archive. The fixture also checks allocation refusal and cancellation. This
is the actual application pipeline, not just the upstream SDK probe. Additional
production-worker and actual LVGL reader tests are recorded below. None is a
physical PSRAM/SD-latency measurement.
Provider bookkeeping, owner controls, task stack, display and driver costs are
additional. The large logical search-read volume remains a hardware acceptance
concern even though display memory is independent of total route length.

```sh
cmake --build .codex-build/tmap-dependency
ctest --test-dir .codex-build/tmap-dependency --output-on-failure
.codex-build/tmap-dependency/navigation/navigation_archive_pipeline \
  /data/sd/maps/tmap/osm/london-osm-z13-17.tmap /data/test/london-20km.tmroute --london
```

The current native suite has 15 passing tests. A real ESP32-S3 cross-compiler
probe in `tests/navigation/compile_embedded.cpp` compiles the production inline
archive/match/view paths without exceptions/RTTI and passes the 240 B individual
stack-frame gate. Compiler frames do not establish full task-stack consumption.

## Production worker and LVGL replay evidence

The production-worker host target compiles the actual
`platform_ui_navigation_runtime.cpp`, `SdTmapStorage` and `SdRouteFile`. Only the
RTOS, physical SD and capability heap are replaced by deterministic host ports.
It tests calculate → pending hidden from list → save → list → release all owners
→ reopen → full archive validation → delete. A second job injects cancellation
while the cancellation mutex admission fails, and verifies cleanup with the
previous saved file preserved. A PSRAM refusal prevents a new job.

| Metric | 32-bit host measurement |
| --- | ---: |
| Production worker external allocation payload peak | 2,143,140 B |
| Worker map and spool read volume through SD host port | 1,542,653,456 B |
| Actual UI Map owner, including 128-segment snapshot | 1,576 B |
| Reader/page/two-cursor external payload while following | 2,160 B |
| Replayed geometry endpoints | 1,170 matched / 0 ambiguous |
| Non-straight geometry cues during replay | 299 |
| View queries across start/middle/end, z12/14/16/18 | 12 |
| View read total / maximum per tested view | 66,016 / 20,064 B |
| Matching read total for replay and final arrival dwell | 3,938,176 B |
| Dense overview views / maximum published screen segments | 4 / 72 |
| Pixel-identical full-geometry oracle comparisons for fitting views | 8 / 8 |
| Live route allocations and open files after close | 0 / 0 |

All tested z16/z18 views fit the fixed screen snapshot. The eight fitting views
produce exactly the same LVGL pixels as the full-geometry host oracle, and draw
callbacks perform zero route reads. Coarser dense views show
the zoom-in state. UI replay injects a partial Busy read every seventh operation;
the file remains unchanged. It also checks stale fixes, sustained deviation,
rejoin, final arrival and cold reopening without a false arrival. The host oracle
loads full geometry only in test code; production code retains bounded owners.
See [machine-readable application evidence](native-navigation-application-evidence.json).

`tests/navigation_ui` contains the actual LVGL reader, creation dialog, navigation
controls and session-policy tests. The runtime's config/active-route swap wiring
is compiled in the firmware; the host tests do not claim to drive the entire
GPS-page global state through physical input. Target build results are recorded
in the application evidence alongside the precise tested scope.

The complete Wio Tracker L2 PlatformIO build finished successfully with exit 0
in 795.892 seconds. Its ELF contains the production navigation submit/save and
native Map poll symbols. PlatformIO reports 96,756 B static RAM and 4,913,237 B
Flash in the 6,291,456 B application partition. The generated 4,913,648 B binary
has SHA-256 `163bf3086e3dbf2623d4578de3006ec5d6a8251cc8476e4c9de82d93a79b7500`.
Static RAM is not runtime high-water. ESP-IDF's producer, SD adapter and both UI
consumer sources also pass the real P4 compiler's syntax checks; a complete P4
firmware build and physical acceptance are not asserted by those checks.

The 2,143,140 B measurement covers allocations routed through the host capability
heap, including the worker state and map pages. It excludes allocator metadata,
real SD-handle/driver allocations, the 4,096-byte task-stack request, RTOS control
blocks, LVGL/display buffers, other application features and concurrent readers.
During candidate activation both old/new readers may coexist briefly. These
figures are admission evidence, not a claim that a device with 2 MiB free PSRAM
can run the whole firmware. Internal RAM, real PSRAM high-water, SD wall-clock
latency and energy still require physical-board measurements. Search read volume
is substantial; do not derive an MCU calculation time from the host runtime.

```sh
cmake --build .codex-build/navigation-ui
ctest --test-dir .codex-build/navigation-ui --output-on-failure
.codex-build/navigation-ui/navigation/navigation_production_worker \
  /data/sd/maps/tmap/osm/london-osm-z13-17.tmap /data/test/london-worker --london
.codex-build/navigation-ui/tmroute_ui_contract \
  /data/test/london-20km.tmroute --london
```

The production-worker host target uses Windows 64-bit file-position APIs and is
enabled for MinGW. The other native pipeline/UI tests are portable. No physical
device flash or route walk is implied by these commands.

## Diagnosing Calculate route failures

Navigation emits bounded ASCII serial diagnostics with the `[Navigation]`
prefix. Trace schema 1 records UI Calculate/validation, each backend admission
attempt, worker creation, selected map paths, phase changes, export, save,
cancellation and terminal outcomes. Each backend attempt has a `request` ID.
Long-running work emits a progress line at most once per five seconds; it does
not log every expansion or geometry point. Logging uses no route-sized buffer
and no SD log file. Arduino USB CDC forwards lines as HostLink `EV_LOG` frames
after HELLO, using the existing bounded single-writer queue. Formatting retains
one 384-byte scratch allocation for the firmware lifetime, allocated in PSRAM
only when a ready USB client first requests diagnostics; allocation failure
keeps UART output without falling back to internal RAM. Concurrent formatting
or a full event queue can drop USB lines. Endpoint names, exact coordinates and
configuration fields are omitted. Memory values are capability-heap snapshots
in bytes, not allocation peaks or a claim that the whole firmware fits them.

Capture serial output before pressing **Calculate route**, through the error
and a few seconds afterwards. At 115200 baud, retain all `[Navigation]` lines
and any neighbouring SD, USB, allocation or reset errors. Capture before the
action: a failed admission has no worker log and cannot be diagnosed by waiting
only for a worker-completion message. Use firmware containing `trace=1`; previous
images cannot gain these diagnostics by replacing SD files.

The optional `tools/hostlink/capture_navigation.py` helper reads native USB CDC
at 115200 baud for 1–60 seconds, validates log-frame CRCs and writes a local
timestamped log. Its only transmitted command is HELLO, which enables the
existing HostLink event session. It waits for HELLO_ACK before declaring capture
ready, changes no configuration and performs no bootloader/reset sequence. It uses the
same native CDC DTR/RTS connection signals as the existing HostLink transport.
Close other applications holding the selected serial port before capture. Use
`--uart` for passive ASCII capture from a physical UART instead. Ordinary UART
`printf` output is not automatically mirrored to the Arduino native USB CDC port.

```sh
python tools/hostlink/capture_navigation.py --port COMxx --seconds 45 \
  --output .codex-build/navigation-ui/device-navigation.log
```

| Trace event / reason | Interpretation |
| --- | --- |
| `[UI] event=calculate` without a submit | Endpoint selection or fresh-current-GPS validation stopped the action; inspect the next UI message/fix fields |
| `psram_unavailable` | No external memory is available to this runtime |
| `invalid_origin`, `invalid_destination` | Coordinate/name validation failed before map reads |
| `sd_not_ready` | SD runtime was unavailable before creating a task |
| `mutex_allocation_failed`, `mutex_busy` | Backend admission could not obtain its state lock |
| `previous_job_retained` | Another job or its terminal state still exists; preceding status/running/cancel fields identify it |
| `state_allocation_failed` | External allocation of worker state failed |
| `task_create_failed` | FreeRTOS rejected task creation; compare internal free/largest block and the 4096-byte stack request |
| `[allocation] rejected` | A search/export provider request failed; requested bytes/alignment and external heap capacity are recorded |
| `mkdir_failed`, `pending_open_failed`, `spool_open_failed` | Working-file setup failed before graph search; inspect SD readiness, medium session and USB ownership |
| `[map] event=candidate` | Exact installed package considered by the worker |
| `[search] event=failed result=...` | SDK returned a search/open/budget/coverage result; unsupported packages can be skipped before trying another candidate |
| `[worker] event=finish status=...` | Final application outcome after route/map owners were released |

Numeric phase IDs follow `ArchiveCalculation::Phase`: Idle 0, Opening 1,
Allocating 2, Beginning 3, Searching 4, Ready 5, ExportBeginning 6, Exporting 7,
Complete 8, Failed 9, Cancelled 10. `usb_owner=1` means USB holds SD ownership
at that observation. A card exposed over USB can pass desktop file checks while
application SD access is unavailable. USB ownership observed during a later
inspection does not prove it was active during an earlier failed calculation.

`tests/navigation/verify_worker_trace.py` fault-injects admission failures through
the actual production worker target and checks distinct rejection reasons,
memory fields, lifecycle logs and omission of private endpoint data. The host
test does not reproduce the physical device's exact heap fragmentation or USB
transition timing. A new reproduction with the instrumented firmware is still
needed to establish which rejection occurred on a particular device.

The 2026-10-16 Wio investigation also found a deterministic work-directory bug:
the worker unconditionally created `/routes`, but SdFat returns false when the
final mkdir component already exists. The card already had that directory.
The worker now checks `sd_is_directory` before creating either working directory;
a regular file at either path still fails. The host worker port now reproduces
SdFat's exclusive final-component mkdir behavior. The unpatched worker failed
with `mkdir_failed` and `storage_unavailable` under that regression; the patched
worker passes existing-parent/existing-work-directory and file-collision cases.
This establishes the source defect and host regression, not successful physical
route calculation after the fix; on-device acceptance remains separate.

The corrected Wio image built successfully in 215.072 seconds (exit 0):
90,096 B static RAM and 5,029,329 B linked Flash. Relative to the first diagnostic
image in the same incident, static RAM increased by 8 B and Flash by 288 B.
The 5,029,744-byte binary has SHA-256
`e03bca86d81190888c346bc45a4fa3960fc86ea0d61ad001819c9c6604107ce8`.
Flashing that image to the confirmed Wio target completed with hash verification
in 67.686 seconds (exit 0). The application CDC port cannot automatically enter
download mode with reboot disabled; manual BOOT/RESET was used for this flash.
The corrected production-worker host regression also completed the Center
London 20,778.6 m case with 1,171 points, 43,776 archive bytes, 2,143,140 B peak
provider payload and zero live allocations after close. That host payload does
not include the Arduino USB scratch/queued frames or physical SD/RTOS overhead.
Successful flashing and host routing do not yet establish a successful route
calculation or runtime memory peak on the Wio itself.

The subsequent Wio CDC reproduction did reach Searching, using the pre-existing
`/maps/navigation/navigation.tmap`, then returned `io_error` after 20,730 ms.
At the failure snapshot SD was ready, USB did not own the medium, the medium
session matched, PSRAM free was 6,656,699 B and internal free was 183,084 B.
These are observations at log points, not runtime allocation peaks. No route
was published. The trace rules out admission failure for that reproduction;
it does not establish map corruption or a complete physical-card diagnosis.

This exposed another desktop/embedded filesystem difference: exFAT `seekSet`
rejects positions beyond the current data length. Reverse leg-spool output can
first write a later record, and archive geometry begins at an aligned offset.
The desktop file ports had allowed sparse seeks. Tightening both host SD ports
to reject beyond-EOF seeks reproduced the same Searching → `io_error` failure
in the actual producer, and the new gap-write adapter case failed as expected.
The adapter now pads at most 256 bytes per call and returns More before retrying
the target write, preserving partial Busy progress. Its padding is a read-only
Flash constant, with no additional route-sized RAM owner. Tests check the
per-call growth limit, zero padding, partial Busy retry and final archive CRCs.
Current integrated map directories are searched before older standalone graph
directories. Fatal map and route adapter I/O now report the operation, offset
and requested byte count without endpoint names or coordinates. These changes
still require their own physical-board acceptance.

The combined directory/gap/selection correction built for Wio in 226.850 seconds
(exit 0), with 90,096 B static RAM and 5,030,413 B linked Flash. The ELF places
the 256-byte padding symbol at Flash-mapped address `0x3c4545ac`; it is not a task
stack or mutable RAM array. The 5,030,832-byte binary has SHA-256
`58430ab89931c1fa337353b1e43e8eb99adf0f987b8851979d86340614f0730d`.
Upload and hash verification completed in 61.342 seconds (exit 0). With strict
exFAT seek semantics, the Center London host run still produces 20,778.6 m,
1,171 points and 43,776 archive bytes, with 2,143,148 B external provider payload,
1,542,653,456 logical read bytes and zero live owners after close. The additional
8 B relative to the prior host run is the map-read observer. Physical route
calculation was subsequently reported successful by the Wio user. No terminal
trace, route length or physical allocation peak was captured for that success.
The user then reported an unavailable/damaged message when opening Map; this
is a separate saved-route read/display issue.

The saved-route investigation found that Arduino's LVGL callbacks discarded
`SdRuntimeFile::read_busy()` on seek/tell and on short reads. A transient bus
admission failure could therefore become an I/O error or a false zero position.
The callbacks now propagate `LV_FS_RES_BUSY`, and `position()` records failed
bus admission like `size()` and `seek()`. A short open retry also handles LVGL's
generic open error; real read errors still fail visibly. Terminal reader logs
record the stage, operation, byte offset and result without private coordinates.
Exceeding a viewport query budget leaves the archive open and asks the user to
zoom in. It no longer classifies density as damage.

The reader/preview stage passed all 15 tests then available, including the production-callback
Busy/read-error regression and a 10,000-point dense-view/zoom recovery case.
The latter keeps the same reader allocation before and after zoom, with zero
open file handles after destruction. Interactive preview tests use the actual
Tracker camera, gestures, controls and key handlers with deterministic map/app
ports. ESP stack hygiene and four ESP-IDF navigation syntax checks also pass.
The subsequent user report confirmed normal operation on the physical Wio.
This is user-reported acceptance, without a captured trace or a measured
whole-firmware memory peak.

The reader/interactive-preview Wio image built successfully in 239.846 seconds
(exit 0), using 90,124 B static RAM and 5,035,053 B linked Flash. Compared with
the preceding spool-correction image, these increased by 28 B and 4,640 B.
This excludes dynamic LVGL/map/reader allocations and does not measure runtime
heap peaks. The 5,035,472-byte binary has SHA-256
`993725458a27f0131b7af7a527f4bda36b570c998b905b7da865d4086a0071c3`.
Upload to the confirmed Wio target and Flash hash verification completed in
61.459 seconds (exit 0). The Wio user subsequently tested the updated image and
reported normal operation. A later USB capture attempt could not open COM69
despite Windows listing it, so no diagnostic capture or additional device
resource measurement is claimed for that acceptance.

Pager keyboard follow-up adds letter-based pan and zoom matching Map, with Fit
and an explicit scrollable Preview Help list. It changes preview downloads from
D to G and the profile from E to V to avoid collisions. The actual Tracker key
handler regression checks both letter cases for all four pan directions,
Q/E zoom and F reset. Pager physical-key acceptance remains separate from the
earlier Wio touch verification.

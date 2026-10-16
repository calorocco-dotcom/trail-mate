# ESP internal RAM and PSRAM audit

Audited on 2026-10-16 for the PR #104 P4, Pager, T-Deck and Wio L2 implementation.
The audit covers linked static data, application heap owners, task stacks, UI
caches, the Arduino SDK, IDF 5.5.4, LVGL, mbedTLS and the P4 C6 companion.

## Evidence and measurement boundaries

The baseline is `976e84510700c38a1cd7ccfdb04f2fb2057f18a7`: it adds reporting
without changing firmware behavior relative to `e73fa53b`. The complete target
reports are artifacts of [baseline CI](https://github.com/vicliu624/trail-mate/actions/runs/38856889758).
Each report contains its source revision, ELF SHA-256, allocated sections and
RAM symbols. P4/C6 reports also retain resolved SDK memory settings; CI asserts
external P4 TLS/LVGL allocation and the C6 RX/NVS profile. Flash sections and
linker dummy/address-gap sections are excluded.
`internal_data` is linked data/BSS/noinit, not the whole application's runtime
RAM use. `internal_code` reports IRAM separately. RTC/TCM are separate again.
Symbol sizes must not be added to section totals: aliases and padding make
those different views of the same allocation.

The first optimization link reports are from `80334689`, available in
[optimization CI artifacts](https://github.com/vicliu624/trail-mate/actions/runs/38857930137).
All firmware links in that run succeeded; its NVS inventory integration check
exposed the source-file-count update corrected in `746d57e3`. Subsequent TCP
close/DMA-owner and test follow-ups are covered by the final PR checks.

| Target | Baseline static internal data, bytes | Optimized, bytes | Reclaimed, bytes |
| --- | ---: | ---: | ---: |
| T-Display P4 TFT | 106,172 | 83,916 | 22,256 |
| T-Display P4 AMOLED | 104,764 | 82,572 | 22,192 |
| Wio Tracker L2 | 97,120 | 90,004 | 7,116 |
| Pager SX1262 | 98,128 | 91,012 | 7,116 |
| Pager LR1121 | 98,552 | 91,436 | 7,116 |
| T-Deck | 94,456 | 87,340 | 7,116 |
| T-Watch S3 | 94,656 | 87,540 | 7,116 |
| C6 companion | 53,056 | 53,056 | 0 |

These are same-toolchain CI measurements. The older local P4 ELF still had a
64 KiB LVGL builtin pool and mutable bzip2 lookup tables, but current PR code
had already removed those costs. Neither is counted as a saving in this audit.
No physical heap peak, stack high-water mark, radio throughput or frame-rate
measurement is inferred from host tests or ELF files.

The local-radio runtime shrank from 2,760 to 1,480 bytes, confirming that the
16 scan records occupied 1,280 static bytes in this SDK. The C6 host runtime
shrank from 20,268 to 3,888 bytes. Final section deltas include small control
state and linker alignment, so they differ slightly from summing raw payload
sizes. IRAM totals did not change. P4 AMOLED RTC/TCM usage grew by 12 bytes;
those bytes are not included in the internal-data column above.

## Implemented allocation and lifetime changes

### P4 TCP proxy

The C6 host runtime previously embedded a 16,384-byte TCP receive ring in its
20,268-byte static internal object. The ring now uses strict PSRAM, allocated
before an OPEN command is sent. A failed allocation leaves no remote socket to
clean up. The existing receive mutex protects allocation, reading, appending
and freeing. SDIO still uses its separate aligned internal DMA scratch buffers.

The ring is released on reset, Wi-Fi loss and empty terminal states. A remote
EOF preserves unread response bytes; the last read releases storage. Explicit
close immediately discards unread bytes, serializes with the receive owner,
and ignores late OPENED/DATA events while closing. Idle Wi-Fi
does not allocate this ring. Tab5 shares this policy with its existing 4 KiB
ring capacity. Locks and transport control remain in internal RAM.
TCP reset, status snapshots, connection IDs and transfer counters share the
receive-buffer lock, while sending and closing serialize with the receive
owner. Linux host tests also overlap reads/status snapshots with 500 reconnect
and reset cycles.

The SDIO transport now explicitly requests aligned internal DMA memory for its
owner object, with matching polymorphic deletion and a recoverable allocation
failure. It embeds DMA scratch and a mutex; ordinary `new` could otherwise
follow the default allocator's PSRAM fallback under pressure. This correction
enforces a peripheral constraint and is not claimed as a memory saving.

### Settings catalogue

The eight settings item tables occupied 5,940 internal static bytes on both
32-bit ESP families. Their templates are now immutable Flash data. The first
Settings visit creates an exactly sized PSRAM catalogue and redirects the
small category handles only after successful allocation. Failure leaves the
templates untouched and exits with a notice; a later visit can retry.

The bound catalogue deliberately retains stable addresses after the page
closes. Existing list, modal and deferred LVGL callbacks borrow those
addresses. Freeing or relocating it at page exit would require a separate view
ownership redesign, including asynchronous deletion. This bounded PSRAM cache
does not own hardware, timers or tasks. It is not a dashboard-widget lease.

### Local Wi-Fi and scanning

The shared local-radio runtime now allocates the 16-record scan scratch only
for a scan, in PSRAM, instead of retaining it in static internal RAM. RAII
releases it on every success/failure return. Scan failure does not consume the
internal reserve. This applies to the Arduino S3 targets; P4 uses the separate
C6 Wi-Fi projection and is not charged this saving.

Both local and C6 scan error paths now explicitly clear the SDK-owned AP list
if fetching the count/records fails. The pinned SDK documents this as required
to avoid leaked BSS scan information, including a failed records fetch under
memory pressure. The local runtime also reports a count-fetch error as failure
instead of presenting it as an empty successful scan. Tests model the driver
list separately from the caller's PSRAM output and fail before this correction.
C6 also emits the Wi-Fi error event so P4 ends its pending scan instead of
waiting for the asynchronous scan timeout after a result-fetch failure.

The primary local Wi-Fi profile had four static RX buffers but inherited a
16-entry AMPDU RX Block Ack window from the prebuilt SDK. It now explicitly
bounds the window to four. The emergency profile still disables aggregation.
Driver NVS is disabled in the init structure because Trail Mate's settings
store owns credentials and already selects `WIFI_STORAGE_RAM`.

The C6 build now also uses four static RX buffers with a four-entry RX BA
window, retaining 16 dynamic RX and 16 dynamic TX buffers. Management short
buffers are bounded to eight and driver NVS is disabled. Reducing eight static
RX buffers to four removes approximately 6.4 kB of driver initialization
allocation according to the SDK's approximate 1.6 kB-per-buffer model. This is a dynamic
driver budget calculation, not a measured on-device heap delta. AP/STA and
ESP-NOW still use their existing shared-radio owner.

The local 32 KiB reconnect reserve, 32 KiB connection free-memory gate and 8 KiB
largest-block gate remain. This reserve protects vendor scan/reconnect paths
that can abort under pressure. It cannot be counted as freely usable heap and
is not safely removable just because large application allocations use PSRAM.

### TLS library policy

On Arduino, the old HTTP implementation replaced mbedTLS's **global** allocator
on the first HTTP request using two unsynchronized static booleans. Other TLS
work could run first, and concurrent first requests could race. Allocator
installation now belongs to startup, after board/PSRAM initialization and before
board services, application background tasks and network clients start.

All mbedTLS calloc requests use PSRAM when available, including small
certificate/key bookkeeping that the old 1,024-byte threshold left internal.
There is no internal fallback on PSRAM-equipped boards. Zero-sized and
overflowing requests return null; calloc zeroing and matching heap-capability
free are preserved. Boards without PSRAM retain an internal path. This uses
the allocator hook actually provided by the pinned Arduino SDK, rather than
pretending that application `-D CONFIG_*` flags rebuild precompiled libraries.

Both P4 panel configurations select IDF's `MBEDTLS_EXTERNAL_MEM_ALLOC`, whose
`esp_mbedtls_mem_calloc` implementation directly requests
`MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`. TLS buffers and contexts therefore do not
consume the internal DMA/task budget. Protocol fragment limits, certificate
validation and crypto algorithms are unchanged.

### LVGL on Arduino PSRAM boards

Small persistent LVGL objects previously preferred internal memory on Pager,
T-Deck and T-Watch; only L2 already preferred PSRAM for them. All PSRAM boards
now use that preference. The existing non-PSRAM path and ordinary allocator
fallback remain for general LVGL work; explicit widget/page/font PSRAM scopes
remain strict. Reallocating an existing external allocation remains external.
Display DMA buffers keep their separate capability allocations. The actual
internal-heap benefit depends on the active screen and is not a fixed ELF delta.

## Other audited owners and deliberate constraints

| Owner | Current allocation/lifetime | Audit decision |
| --- | --- | --- |
| P4 MIPI/PPA and USB/SDMMC DMA | Explicit driver-owned buffers/descriptors; P4 USB MSC endpoint buffer is 8 KiB static | Retain capability/alignment requirements; shrinking transfer buffers without bus/throughput evidence is not a RAM-only change |
| SD filesystem | Bounded DMA bounce buffer, PSRAM file/iterator storage on equipped targets | Existing SDMMC batching and storage session ownership are retained |
| Music | Strict PSRAM runtime, decoder workspace/input/artwork/lyrics; lazy 4 KiB internal worker stack | Idle widgets do not start SD scans/tasks; hidden paused state releases the worker and decoder; playing background audio remains user-owned |
| Menu widgets | Strict PSRAM state; only visible-widget timers/subscriptions/heading leases | No duplicate background services or pre-instantiated hidden widgets were added |
| Map tiles | PSRAM decode/scratch/POI payloads with bounded profiles | Last viewport cancels work; worker frees scratch/metadata and deletes its task; P4's bounded decoded cache may remain for reuse |
| Geocaching | Explicit PSRAM session/catalogue/workspace/page/protocol buffers | Preserve the existing ownership model and admission fixes; see the separate Geocaching memory audit |
| Mesh/MQTT | Shallow radio queues; packet payloads and MQTT runtime prefer external allocation through the existing budget layer | Keep RTOS queue controls and radio scheduling internal; no unbounded queues introduced |
| GPS/motion | Service state prefers PSRAM; collection and motion tasks are service-owned | A widget must not start a second receiver; GPS-off semantics still belong to the GPS policy, not view deletion |
| Voice/Reticulum call | Codec/audio buffers use external allocation; 20 KiB capture and playback stacks are internal | Keep stack safety margins without hardware high-water evidence for every codec/error path |
| SDK Wi-Fi/lwIP | Prebuilt Arduino driver has fixed static FTM/NVS/DNS state; runtime config controls selected dynamic pools | Application flags do not remove those linked library globals; a custom SDK build would need separate compatibility and throughput acceptance |
| Lookup/icon/font data | Current bzip2 tables and icons are const/mapped Flash; UI/font caches use existing bounded profiles | Do not confuse Flash symbol sizes or already-landed fixes with new RAM savings |

Moving task stacks, portMUX/atomic control blocks, or whole hardware owner objects
to PSRAM indiscriminately is unsafe. ESP stack/cache restrictions and peripheral
capabilities differ by chip and SDK. No global malloc threshold was lowered:
explicit data owners can move without changing allocation policy for every
third-party synchronization object or peripheral.

## Verification and reproduction

`tests/ram` executes production TCP and Wi-Fi methods extracted at configure
time and the production TLS allocator. It covers repeated ring wraparound,
overflow, EOF draining, failed OPEN sends, allocation failures, Wi-Fi loss,
scan terminal paths, immutable catalogue retry/stable addresses, calloc zeroing,
overflow rejection and no internal fallback. These are deterministic ownership
tests, not an emulation of vendor RF or FreeRTOS scheduling.

The existing LVGL allocator tests check small/large and no-PSRAM paths. The
production LVGL/Wasm builds render both P4 panels and the compact boards; menu,
settings, music and controls regressions exercise real UI construction. ESP
stack hygiene and clang-format 14 are required. Full target linking checks the
actual SDK APIs and configuration choices.

`tests/site/settings-catalog-native.test.mjs` additionally visits every visible
settings category on all five displays, opens dynamic locale/timezone options,
saves timezone changes and verifies bindings after normal page reconstruction.
Compact Pager rows are reached with the encoder; touch rows are reached by
swiping. The test repeats visits rather than relying only on a first-render
screenshot.

Every firmware CI build now uploads `ram-report-<target>` for seven days. To
reproduce locally with an existing matching ELF and `pyelftools` installed:

```sh
python scripts/report_elf_ram.py path/to/firmware.elf \
  --revision "$(git rev-parse HEAD)" --output ram-report.json
cmake -S tests/ram -B .codex-build/ram-audit/native -DCMAKE_BUILD_TYPE=Debug
cmake --build .codex-build/ram-audit/native
ctest --test-dir .codex-build/ram-audit/native --output-on-failure
```

Physical acceptance should record internal free/largest/minimum, DMA
free/largest/minimum, PSRAM free/largest/minimum, and task high-water marks at
boot, disconnected Wi-Fi retries, scan, HTTPS/OTA, map/music transitions and
voice interruption. The existing memory snapshots expose these heap domains.
Combine weak-signal/AP-loss and SD/USB lifecycle stress before reducing any
remaining internal reserve or task stack.

## Primary references

- [ESP-IDF 4.4.7 S3 Wi-Fi buffering and performance](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-guides/wifi.html#wi-fi-buffer-usage)
- [Pinned IDF 5.5.4 Wi-Fi Kconfig](https://github.com/espressif/esp-idf/blob/v5.5.4/components/esp_wifi/Kconfig)
- [Pinned IDF 5.5.4 mbedTLS allocation implementation](https://github.com/espressif/esp-idf/blob/v5.5.4/components/mbedtls/port/esp_mem.c)
- [P4 capability-aware heap allocation](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32p4/api-reference/system/mem_alloc.html)
- [P4 external RAM restrictions](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32p4/api-guides/external-ram.html)

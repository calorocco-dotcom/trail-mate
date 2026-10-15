# Released TMAP dependency

Trail Mate consumes the independent [TMAP SDK](https://github.com/vicliu624/tmap)
as a third-party dependency. The application owns filesystem/storage adapters,
UI policy and task scheduling. SDK readers and algorithms are not maintained as
a second copy under `modules/core_tmap`.

## Locked release

- Version: `v0.1.1` (experimental pre-release).
- Commit: `d8fc21550859bce1c242e6330b0e9ae2ddf9eecc`.
- Source archive SHA-256: `d8377999b30703e870f9e5dab09f7de89eb6474cc01930fc3d330de6e85acb77`.
- License/provenance: upstream AGPL-3.0-only, LICENSE, COPYRIGHT and NOTICE.

PlatformIO declares the immutable Git dependency in `platformio.ini` and the
consuming `ui_map_runtime/library.json`. This uses PlatformIO's normal package
manager; no script copies headers into modules or relies on a sibling checkout.

CMake uses `cmake/TrailMateTmap.cmake`, which downloads the published source
archive through FetchContent and verifies its SHA-256. Consumers link
`tmap::tmap`; include directories and compile definitions propagate through the
target. ESP-IDF links it after component registration, outside early dependency
expansion. Linux common targets and native TMAP tests use the same helper.

Host producer tools, SDK test suites/examples and the SDK stdio adapter are
disabled for application builds. Existing Trail Mate storage adapters remain
the file-I/O owner. The upstream bounded-stack/no-exception configuration is
enabled. The initial configure/install needs network access; subsequent builds
reuse normal package-manager/FetchContent caches. CMake's documented
`FETCHCONTENT_SOURCE_DIR_TRAILMATE_TMAP` override can use an explicitly supplied
offline source tree; verify it matches the locked release before use.

## Scope and compatibility

Existing map display/search still uses the v1 Reader API. Reader/search source
content is preserved upstream; the SDK header's administrative adapter friend
does not change Reader layout. The larger SDK is now available for subsequent
outdoor feature requirements. Merely linking it does not add a routing UI or
make existing firmware select outdoor-v2 files. MCU buffers, stacks and PSRAM
allocation policy remain application integration responsibilities.

Native validation uses the released archive, tests administrative paths and
links the profile/routing/admin interfaces, including external-memory failure.
The dependency contract also checks that PlatformIO and CMake pins agree and
that no old reader/search implementation is compiled. These three tests run
without map datasets in the dedicated Released TMAP dependency CI workflow.
Optional real world/China probes validate exact pixels, annotations, search,
Busy, session changes, CRC, capacity, cancellation and storage ownership. The
catalog test accounts separately for the reader workspace and metadata holding
three annotation continuation slots; it drains incremental annotation snapshots.

On the tested 32-bit host ABI, the real-map probe retains a 14,024 B v1 Workspace
and a 120 B Reader. The two-package catalog retains 25,368 B in two allocations,
including existing annotation continuation metadata; it is not whole-device RAM.
World/China pixels, annotations, indexed search and page counts match the recorded
upstream baseline. The normalized reader/search source SHA-256 values match the
original module as well. The optional real-map configuration passes six tests.

```sh
cmake -S tests/tmap -B .codex-build/tmap-dependency -DCMAKE_BUILD_TYPE=Release
cmake --build .codex-build/tmap-dependency
ctest --test-dir .codex-build/tmap-dependency --output-on-failure
```

Set `TRAILMATE_TMAP_WORLD_MAP` and `TRAILMATE_TMAP_CHINA_MAP` to local matching
real files to enable the additional three data-dependent tests. These large
datasets are not vendored or downloaded by the dependency helper.

For upgrades, verify a published release and CI, update both PlatformIO pins and
the CMake version/commit/archive hash together, then rerun native contracts and
the target firmware build. Never substitute a moving branch or edit files in
`.pio/libdeps` / `_deps` as the integration source of truth.

## Target build evidence

The `tlora_pager_sx1262` PlatformIO build completed successfully on 2026-10-15
using the installed PlatformIO 6.2.0 environment. The dependency graph reports
`tmap @ 0.1.1+sha.d8fc215` and its checkout resolves to the locked full commit.
Compiler dependencies resolve `tmap/reader.h` from the managed SDK package.
The final firmware map contains only `reader.cpp.o` and `search.cpp.o` from
`libtmap.a`, with no `libcore_tmap.a`. Unused outdoor/routing archive objects
are not pulled into the current application by this dependency migration.

The successful build reports 97,356 B static RAM and 4,662,485 B Flash. These
are whole-build linker figures for this working branch, not an isolated TMAP
delta, dynamic heap/PSRAM peak or hardware measurement. No device was flashed.
The earlier attempt with a different local PlatformIO 6.1.19 installation failed
on a SCons tool-module import before compilation; it is not counted as success.
Native CMake validation includes all six optional real-map tests. Full Linux
application and ESP-IDF firmware builds remain separate target checks; their
build entrypoints now link the same pinned dependency through CMake.

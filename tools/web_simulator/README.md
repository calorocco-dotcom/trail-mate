# Native firmware preview for GitHub Pages

This target compiles the real `modules/ui_shared` LVGL pages, fonts, images,
layouts and event handlers. The web page displays their RGB565 framebuffer.
It does not reproduce the device interface using HTML/CSS, nor swap screenshots.

Pager, T-Deck and Wio Tracker L2 are separate WebAssembly executables, compiled
with their respective board definitions. Pager uses 480 × 222; the other two
use 320 × 240.
LVGL 9.4.0 matches the firmware's PlatformIO dependency. Emscripten is pinned to
4.0.10. Browser deployment requires only static JS, WASM and data assets and
does not depend on pthreads, cross-origin-isolation headers, or a server process.

## Build and view

In an activated Emscripten SDK shell, from the repository root:

```sh
emcmake cmake -S tools/web_simulator -B build/web-simulator -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/web-simulator --parallel 4
node tests/site/native-ui.test.mjs
python -m http.server 8765 --directory site --bind 127.0.0.1
```

Open `http://127.0.0.1:8765/?feature=home&demo-device=pager#explorer`.
Use `demo-device=tdeck` or `demo-device=wio-tracker-l2` for the other builds.
Website language selection is separate from the native firmware language.

The Pages workflow builds all three profiles before publishing. Native outputs are
generated in `site/assets/native`, which follows the existing generated-asset
ignore policy. `manifest.json` versions each complete JS/WASM/data set so a
browser cannot accidentally mix cached artifacts from different builds.

When local PlatformIO dependencies exist, CMake uses their LVGL/Nanopb trees;
otherwise it fetches pinned upstream releases. The local SDK location is not
part of this repository's configuration. Windows can pass its installed SDK's
`cmake/Modules/Platform/Emscripten.cmake` as `CMAKE_TOOLCHAIN_FILE`.

## Boundaries

- The original app catalog constructs the menu metadata. The original menu,
  page profiles and widgets construct and rasterize the visible interface.
- `native_*` files provide the browser app shell, lifecycle routing,
  framebuffer transport, pointer/keyboard input and accessibility projection.
- `preview_*` files provide demo hardware/service data. Storage uses the
  WebAssembly virtual filesystem. No serial port, BLE, radio or microphone is
  opened. Package installation is explicitly unavailable in the preview.
- Chat uses the production view classes with sample conversations and local
  preview navigation. It is not a validation of the full firmware chat delivery
  pipeline. Team pairing and real cryptographic/network interoperability still
  require devices and their existing tests.
- The map viewport, controls and overlays are native. A synchronous browser
  storage adapter loads the cached tiles, replacing the hardware-specific SD
  worker. Shared Mercator geometry is reused. Nine attributed OSM tiles cover
  the sample position at zoom 12; other areas/zoom levels can be missing.
- `data/sd/routes/sample-route.kml` is a synthetic route for UI testing. It is
  not travel guidance. `prepare_demo_tiles.py` is a one-time fixture maintenance
  tool and is not run by normal builds or Pages CI.
- Protocol Probe receives no fabricated packets. A quiet simulated receiver
  cannot create OBSERVED or CONFIRMED evidence.
- MQTT is reached through the real Settings page, and route previews through
  Tracker. HostLink is not exposed on the website. Device state values,
  supported actions and firmware verification are
  distinct from the visual fidelity of the rendered pages.

The final linker uses `-O0` because the Windows Binaryen optimizer in the pinned
SDK crashed during the local optimized link. C++ compilation still uses `-O2`.
This affects artifact size, not the identity of the page source being compiled.

## Verification

Optional P4 TFT/AMOLED targets use the same production pages at 1168×540 and
1232×568. Configure with `TRAIL_MATE_PREVIEW_P4=ON` and firmware LVGL 9.5.0,
then build `trailmate_p4_tft` and `trailmate_p4_amoled`. See the
[P4 layout and dashboard report](../../docs/engineering/t-display-p4-ui-audit.md)
for frame capture, navigation checks and lifecycle stress reproduction.

`tests/site/native-ui.test.mjs` instantiates the generated WebAssembly, exercises
real calculator controls, checks the second function layer and DEG/RAD, and
enters/exits 19 page states for all three profiles. Browser checks additionally cover
resource loading under a subpath, pointer/text input, layout and scaling.

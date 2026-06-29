# Waveshare ESP32-S3 1.43" AMOLED — Reusable Board Emulator

Goal: a standalone emulator for the Waveshare ESP32-S3 Touch AMOLED 1.43" board
that runs **any** Arduino sketch built against this board's standard library
files (`amoled.h`, `FT3168.h`, `qmi8658c.h`), not just Trailmaster-OS. The
sketch is compiled completely unmodified; only the hardware underneath it is
swapped out.

## Layers

| Layer | What | Reusable across | Status |
|---|---|---|---|
| L0 | SDL display canvas + mouse-as-touch | any program | **done & folded into L3** (`emulator/runtime/main.cpp`) |
| L1 | Board BSP shim: `Amoled`, `FT3168` (`getTouch`), `qmi8658c` — same public API as the real driver files | any sketch on this board | **done & verified** (`emulator/board/*_sim.cpp`) — compile clean standalone against the real, unmodified headers |
| L2 | Arduino-ESP32 core shim: `Serial`, `millis`/`delay`, `Preferences`, `WiFi`/`esp_wifi` (fake, never connects), `WebServer`/`DNSServer` (type-only), `FFat`, `esp_heap_caps` (malloc-backed), `AnimatedGIF` (stub), `sdcard_shim` (`/sd_card` path redirect), FreeRTOS task fns | any ESP32 Arduino sketch | **done for this pass** (`emulator/core/`) |
| L3 | Runtime bootstrap: call `setup()` once, `loop()` forever, pump SDL/LVGL events between | any program | **done & verified end-to-end** (`emulator/runtime/main.cpp`) |
| L4 | The actual sketch (`.ino` + supporting files) | swappable per project | **MILESTONE: real .ino compiles, links, runs, and renders correctly** — see "Day 2 result" below |

**Emulator = L0+L1+L2+L3.** L4 is a `--sketch <path>` argument, not baked in.

## Day 2 result (milestone)

The real `waveshare_esp32s3_1.43_amoled_lvgl8.ino` — with exactly **one**
mechanical, build-time-only transform (never edits the tracked file; see
`emulator/build.sh`'s header comment and "Known quirks" below) — compiles,
links, and runs as a native macOS app. Screenshot-verified: the real
`build_settings_screen()` renders pixel-correct (brightness buttons, Wifi
settings row, Jimny mode / Grid Launcher toggles, dark theme).

**Run it:** `./emulator/build.sh && ./emulator/build/trailmaster_emulator`

**What this pass deliberately does NOT compile** (stubbed in
`emulator/board/firmware_stubs.cpp`, matching `sim/`'s existing precedent):
- `PhotoFrameApp.cpp` — WiFi portal + photo carousel
- `OTAManager.cpp` — real network OTA (same animated-state stub as `sim/`)
- The GIF/PSRAM parts of `ui_godzillaspeedometer.cpp` (screen init/destroy/
  event handler) — the pure-visual parts (gauge, ticks, settings menu) ARE
  real, shared via `godzilla_speedo_ui.h`
- NES/SMS game engines (`RetroEngine`/`NesEngine`) — `screen_game.cpp`
  (Dino/Flappy) IS real and compiled

**Known quirks found along the way:**
- `g++` defaults ALL inputs to C++ regardless of `.c` extension unless told
  `-x c` explicitly (same class of bug as `emcc` vs `em++` from the `sim/`
  WASM build) — SquareLine's `.c` files need this.
- `esp_heap_caps.h` must be C/C++-agnostic — LVGL's own `.c` sources pull it
  in transitively.
- One redundant local `extern bool dino_ready;` (line 1790 of the .ino)
  has a linkage conflict with `screen_game.h`'s file-scope declaration that
  **Clang treats as a hard error with no controlling flag**, but the real
  ESP32 GCC toolchain accepts leniently. Real GCC (installed via brew) was
  tried as a "more faithful" fix but hit an unrelated, deep Homebrew-GCC/
  macOS-SDK incompatibility (fixincludes assumes an older SDK header
  layout) — not worth fixing further. Settled on a build-time-only sed
  transform (one line, in `emulator/build.sh`, applied to a generated copy
  in `emulator/build/`) — the tracked `.ino` is never touched.
- `ui_gridlauncher.c` is genuinely dead/vestigial SquareLine code —
  `ui_uigridlauncher` is declared but never referenced anywhere in the
  real `.ino`'s actual flow (the real grid launcher is the hand-coded
  `build_grid_launcher()`, built on `ui_uilauncher`). The real toolchain's
  `--gc-sections` silently strips it; native linking needs it excluded
  explicitly from the build file list.
- `open_speedo_settings_menu` (inline, in the shared `godzilla_speedo_ui.h`)
  needed a non-static, externally-linked global holding its address to
  prevent dead-code elimination — its only caller in this reduced build is
  a `.c` file, so there was no "real" call site forcing emission.

**Update (next session):** the "boots to Settings, not speedometer" note
below was a **false observation** — a stale/leftover process window from
inconsistent cleanup between test runs, not real emulator behavior. A clean
re-run (kill any old process first, then run+screenshot) correctly shows the
**Speedometer** screen at boot (TRAILMASTER badge, gauge ring, "0 km/h",
"0 rpm" — no live OBD data since WiFi never connects, as expected with the
stubs). No bug here. Always `pkill -f trailmaster_emulator` before a fresh
run+screenshot to avoid this confusion again.

## Day-by-day

- **Day 1 (today)** — scaffold `emulator/`; write L2 (pure C++, no ESP/WASM
  toolchain needed, compiles with plain `g++` in milliseconds); prove it with
  a trivial native test. No heavy compiles today (quota-aware).
- **Day 2** — write L1. Key finding from reading the real headers:
  - `amoled.h` declares `esp_lcd_panel_handle_t panel_handle` and
    `esp_lcd_panel_io_handle_t io_handle` as **private** members, and
    `#include`s 5 ESP-IDF headers (`esp_lcd_panel_interface.h`,
    `esp_lcd_panel_io.h`, `esp_lcd_panel_vendor.h`, `esp_lcd_panel_ops.h`,
    `esp_lcd_panel_commands.h`) just to get those opaque types. We don't need
    real functionality from them — just enough typedefs for the header to
    parse. amoled.cpp (the real implementation, which calls the actual
    esp_lcd_panel_* functions) is **never compiled** in the emulator; we
    write a substitute `amoled_sim.cpp` implementing the same 8 public
    methods (`begin`, `drawBitmap`, `drawArea`, `fillScreen`, `fillRect`x2,
    `invertColor`, `setBrightness`, `ID`, `name`) writing straight to the L0
    SDL texture.
  - `FT3168.h`: trivial — 2 functions (`Touch_Init`, `getTouch(uint16_t*,
    uint16_t*)`). Maps directly onto L0's existing SDL mouse code.
  - `qmi8658c.h`: a large block of register/enum definitions, but the actual
    function surface to stub is small (`qmi8658_init`, `qmi8658_enableSensors`,
    `qmi8658_read_sensor_data`, etc.) — return fixed/fake accel+gyro values.
  - Also check `low_level_amoled.h` / `board_config.h` (amoled.h's other
    includes) and `i2c.h` (FT3168.h's include) for anything else needed.
  - Swap in the **real** Trailmaster `.ino` (zero edits) and grind the
    compile-fix loop until everything links. This is the bulk of remaining
    effort.
- **Day 3** — verify all screens render via screenshots through the real
  `setup()`/`loop()` (not hand-built sim scaffolding). Confirm the
  grid-launcher-overlap bug class is structurally gone. Handle leftover
  edge cases (real GIF decode is probably still a placeholder — AnimatedGIF
  itself is portable C and might "just work" once SD/heap_caps are faked,
  worth trying before assuming we need a stub).
- **Day 4** — polish, write a short README for using the emulator with a
  *different* sketch, decide how it lives long-term (own repo? subfolder?).

## Day 3 progress: multi-screen verification

Added a debug-only hook in `emulator/runtime/main.cpp` (emulator-owned file,
not the firmware): `EMU_FORCE_SCREEN=gauge|inclinometer|launcher` env var
jumps to that screen right after `setup()`, so each screen can be
screenshot-verified without simulating real touch/swipe gestures. Required
wrapping the declarations in `extern "C" {}` to match the real SquareLine
headers exactly (same linkage-mismatch class as before — these screen
object pointers and `_screen_init` functions ARE wrapped in `extern "C"`
in `ui_uigauge.h`/etc., easy to miss since the wrapper starts a few lines
down, not at the very top of the file).

**Screenshot-verified correct, via the real firmware code (not sim/'s
hand-rebuilt approximations):**
- **Speedometer** (default boot screen) — TRAILMASTER badge, gauge ring, 0 km/h / 0 rpm
- **Gauges** — 0% engine load, 0°C coolant, **13.8V** battery (matches the
  firmware's exact `car_voltage = 13.8` default — proves the real OBD
  globals/logic are live, just never overwritten since WiFi never connects)
- **Inclinometer** — 0° reading (matches qmi8658c_sim.cpp's fixed
  "lying flat" fake IMU data), dual roll-arc gauges, Jimny silhouette
- **Launcher** (grid mode) — all 7 app icons (SPEEDO/GAUGES/INCLINE/IMAGE/
  GAMES/SYSTEM/ABOUT), steel borders, no text bleed-through

**Settings/About — done.** `EMU_FORCE_SCREEN=settings|about` now works
(`emulator/runtime/main.cpp`). One correction to the note this section used
to have: `build_settings_screen()`/`build_about_screen()` are plain C++
functions, but the **.ino's own forward declaration of them sits inside an
`extern "C"` block** (`.ino` line ~99-103, alongside `build_rom_menu()`
etc.) — that's what actually governs the link-time symbol name, so
`main.cpp`'s declaration had to be `extern "C"` too, or the link failed
with "declaration possibly missing `extern \"C\"`". The previous session's
note claiming no `extern "C"` was needed was wrong.

Screenshot-verified:
- **Settings** — Brightness +/-, Wifi settings row, Jimny mode toggle, Grid
  Launcher toggle, dark theme — matches real device.
- **About** — page 1 (gesture hints: swipe-down-to-exit arrow, left/right +
  long-press hint, pagination dots) renders correctly.

**OTA overlay — accepted as sufficiently verified without going deeper.**
`show_ota_update_overlay()` is `static` (internal linkage) inside
`ota_overlay_ui.h`, so it can't be reached via an extern declaration from
`main.cpp` — confirmed, matches the original note. The "Check for Update"
button that opens it lives on About's **page 2** (horizontal swipe target,
`.ino` line ~897), which would need a multi-frame simulated touch-drag
through `g_emu_input` (touch-down + incremental move + touch-up across
several `loop()` calls) to scroll to, since LVGL's scroll-snap needs real
indev polling over time, not a single jump. Decided not to build that:
`OTAManager` itself is stubbed this pass (no real network OTA code behind
the button to exercise), so reaching the overlay would only prove the
button is clickable, not exercise new logic. Page-1 About screenshot above
is the accepted verification for this screen.

Image Frame / Games are deliberately stubbed (PhotoFrameApp.cpp /
RetroEngine/NesEngine not compiled this pass) — expected to render blank,
not a bug.

**Process note:** if screenshots come back showing the macOS desktop/lock
screen instead of the emulator window, check whether the screen is
actually locked (`screencapture` silently captures the lock screen) before
assuming a rendering bug.

## Notes / decisions
- This lives in `emulator/` at the repo root (not nested in the sketch
  folder), to make the "reusable, not Trailmaster-specific" intent visible
  in the layout.
- The existing `sim/` (WASM, per-screen extraction) stays as-is for now —
  not deleted. Once `emulator/` can render the real app end-to-end, decide
  whether to retire `sim/` or keep both (sim/ is faster to iterate visuals
  with `Module.ccall`; emulator/ is the source of truth for correctness).

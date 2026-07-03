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

NES/SMS games (RetroEngine/NesEngine) remain deliberately stubbed — expected
to render blank, not a bug. Image Frame is no longer stubbed — see Day 4.

**Process note:** if screenshots come back showing the macOS desktop/lock
screen instead of the emulator window, check whether the screen is
actually locked (`screencapture` silently captures the lock screen) before
assuming a rendering bug.

## Day 4: Godzilla speedometer + PhotoFrameApp (WiFi overlay, image carousel)
made real — everything except NES/SMS and OTAManager's real network OTA

User's framing for this pass: this is meant to be a generic board emulator
("drag and drop any .ino and it should work without manual work from the
user side") — so the bar isn't "make Trailmaster's two named screens look
right," it's "fix the generic L1/L2 shim gaps that were silently swallowing
real firmware behavior." Two real bugs in our OWN shim code were found and
fixed this way, not worked around in the firmware.

**ui_godzillaspeedometer.cpp and PhotoFrameApp.cpp now compile for real**
(previously wholesale-stubbed in `firmware_stubs.cpp`). Required:
- Vendoring the real AnimatedGIF v2.2.0 library (Apache-2.0, portable C/C++)
  into `emulator/board/vendor/AnimatedGIF/` from the installed Arduino
  library — replacing `emulator/core/AnimatedGIF.h`'s fake (deleted). The
  library's own header already self-detects `__MACH__`/host builds and
  skips pulling in `<Arduino.h>` — by design, since plain desktop use of the
  library shouldn't assume Arduino is present. That meant files which only
  got `Serial`/etc. *transitively* via `<AnimatedGIF.h>` on real ESP32
  (because there `__MACH__` isn't defined, so it falls through to
  `#include <Arduino.h>`) lost that on our host build. Fixed generically by
  force-including `core/Arduino.h` into every C++ TU in `build.sh` (`-include`),
  matching what arduino-cli effectively does sketch-wide — not a per-file fix.
- New generic L2 shims: real `WebServer`/`DNSServer` API surface (`.on()`,
  `.send()`, `HTTPUpload`, etc. — registered but inert, since there's no
  real listening socket; matches the existing "WiFi never truly connects"
  boundary), `esp_cache.h` (no-op `esp_cache_msync`), `byte` typedef,
  `<unistd.h>` for `unlink()`, and an Arduino `String` subclass (was a bare
  `std::string` alias) adding `.endsWith()`/`.isEmpty()`.
- `Jimnylogo.c`/`wifi_qr.c` (SquareLine image assets used by the boot splash
  and WiFi-upload-overlay QR) added to `build.sh`'s C compile list — not
  matched by the `ui*.c` glob.

**Real bug #1 found and fixed: `sdcard_shim.h` only redirected `fopen()`.**
`opendir()`/`stat()`/`unlink()` calls against `/sd_card/...` (used by
`scan_images()` to list photos, and by the splash/Godzilla GIF-existence
checks) were hitting the *real* macOS path `/sd_card` (which doesn't exist)
and silently no-op'ing — e.g. the photo carousel always looked empty no
matter what was on the simulated SD card, even though `img_trailmaster.bin`
(434312 bytes = exactly 466×466×2 raw RGB565, picked up by `scan_images()`'s
`.bin` filter) was sitting right there in `sd_files/`. Fixed by extending
the same path-rewrite macro trick already used for `fopen` to `opendir`,
`stat`, and `unlink` (same file, same pattern: real-call wrapper function
defined *before* its `#define`, so the wrapper's own internal call to the
real libc function isn't itself macro-expanded). This was a gap in our own
emulator code, not a firmware workaround — confirmed fixed: the carousel
now actually shows the trailmaster image.

**Real bug #2 found and fixed: setup()-time rendering was invisible.**
The boot splash (`show_boot_splash()`, called from `setup()`) plays a GIF or
static logo via a *blocking* `while` loop that calls `lv_timer_handler()`
directly — a normal, valid pattern, since on real hardware the LVGL flush
callback IS the physical display, so this shows up fine. But the emulator's
old `main.cpp` only pushed `g_emu_framebuffer` to the actual SDL window
once per `while(running)` iteration, which never runs during `setup()` — so
anything rendered before `setup()` returns (the entire splash sequence) was
invisible; the window would show nothing until boot finished. Fixed at the
L1/L3 boundary, not in firmware: added `g_emu_present` (a plain function
pointer in `emu_state.h`, so L1 stays free of direct SDL calls per that
file's existing design) that `amoled_sim.cpp`'s `drawArea`/`fillScreen`/
`fillRect` call after every write. `main.cpp` registers it to a real
present-and-pump-events function *before* calling `setup()`. Confirmed
fixed: TRAILMASTER splash now visibly renders during boot.

**WiFi-upload overlay (the "wifi overlay screen") confirmed real and working**
via the .ino's existing first-run-onboarding path (no emulator-side
special-casing — an earlier attempt to suppress onboarding specifically
under `EMU_FORCE_SCREEN` was reverted at the user's request: "we don't want
the functionality to be suppressed... this is an emulator, we should not
custom do stuff"). QR code, Wi-Fi toggle switch (auto-ON from onboarding),
SSID/password hint text, and close button all render and respond to clicks.

**`EMU_FORCE_SCREEN` gained `godzilla` and `imageframe`** (alongside the
existing screens) by adding two more `extern "C"` declarations + branches
in `emu_debug_force_screen()` — same pattern as before.

**Double-clickable launch.** `build.sh` now also assembles a minimal
`emulator/build/Trailmaster Emulator.app` bundle (Info.plist + the same
binary) so the emulator can be opened from Finder/`open` without touching
Terminal for normal use — Terminal is still needed once, to run
`./emulator/build.sh`.

**Still stubbed, deliberately:** NES/SMS engines, and OTAManager's real
network OTA (real HTTPS calls to GitHub + ESP32 flash-partition writes —
neither has a desktop equivalent worth building; the existing animated-stub
overlay already covers what's screen-observable). User confirmed this
boundary explicitly when asked.

## Day 5: emulator hardening, then first real use for UI iteration

**Part A — two more emulator-side fixes, both committed in
`f2f677f`:**
- `RetroEngine` (`retro_engine.cpp`) was wholesale-stubbed alongside the
  actual NES/SMS CPU emulation in `firmware_stubs.cpp`, but it's just a
  generic 233x233 framebuffer renderer (`heap_caps_malloc` + `amoled.
  drawArea`, both already real) shared by Dino/Flappy
  (`screen_game.cpp`) — not NES-specific. Compiling it for real is what
  made Dino/Flappy stop rendering blank.
- That surfaced a real performance bug: a single Dino/Flappy frame calls
  `Amoled::drawArea()` ~466 times (once per scanline pair) via
  `RetroEngine::flush()`. `g_emu_present` (added Day 4) presented on every
  one of those calls, and — separately — its unbounded
  `while (SDL_PollEvent())` drain loop could get stuck draining a
  continuous mouse-motion stream (observed specifically when the emulator
  window was focused, in this sandboxed/remote-display environment),
  starving the actual render/game-tick from ever running. Symptom: "static,
  then slight movement" only while focused, smooth once focus moved away.
  Fixed by throttling presents to ~120fps AND capping the event-drain count
  per call (both in `main.cpp`'s `emu_present()`) — not just one or the
  other; throttling only the present still left it stuttering.
- **Not re-verified after the fix** — the user asked to park Dino/Flappy
  and pivot to UI work right after this landed. Re-check it next session.
- `sdcard_shim.h` also gained `access()`/`rename()` redirects (only
  `fopen`/`opendir`/`stat`/`unlink` were covered before) — this is what let
  the Settings screen's boot-image toggle (Part B) actually work.
- `build.sh`'s one build-time `.ino` transform (the `dino_ready` extern
  strip) now matches by content via `sed`, not a hardcoded line number —
  the line had already silently drifted once after unrelated `.ino` edits,
  which would have produced a confusing wrong-line-transformed build.

**Part B — first real use of the emulator for its intended purpose:
iterating on firmware UI with live screenshot verification.** Committed in
`07a7e2a`. This is a genuinely different mode of work than Days 1-4 (which
were about making the emulator itself correct) — worth its own process note
for next time:

*Workflow that worked well:* for a UI change, mock it up first with the
`mcp__visualize` tool (a quick HTML/SVG sketch at the real 466x466 round
proportions) to agree on direction before touching any LVGL code, then
implement for real in the `.ino`/`.cpp`/`.h` files and verify with
`EMU_FORCE_SCREEN=<name>` + a screenshot. Real firmware files are fully
fair game to edit for actual feature work — the "never edit tracked files"
rule is specific to the emulator's own build script needing to compile the
.ino *unmodified*, not a ban on developing the firmware itself.

*Changes made:*
- **Settings screen** (`build_settings_screen()`): grouped rows under
  left-aligned section headers (Display/Connectivity/Modes/Boot image),
  switched row labels from the title's Rajdhani font to Montserrat. The old
  "Custom boot img" toggle was *silently broken in the emulator* (used
  `access()`/`rename()`, not redirected by `sdcard_shim.h` at the time) —
  fixed by the Part A shim fix, not a UI change. A picker-screen redesign
  was tried first and reverted: picking *which* image is already handled by
  the real "SET AS BOOTLOADER" long-press button in Image Frame
  (`PhotoFrameApp.cpp`) — Settings only needed to be the on/off switch for
  whatever's already chosen there, which is what shipped. "Boot duration"
  now always shows (it affects the default splash too, not just a custom
  image — the old hidden-unless-toggled-on layout obscured that).
- **WiFi upload overlay** (`pf_show_upload_overlay`): close button enlarged
  72px/repositioned off the bezel edge; "Enable Wi-Fi:" + toggle centered as
  one flex-laid-out unit (was hardcoded off-center); toggle resized to
  match Settings' switches exactly (60x30).
- **Speedometer/gauge background** (`ui_img_1093738210`, shared by both):
  replaced the old 200x199 stock contour-map texture (needed 800/256x zoom
  + 70/255 black recolor to fill the screen) with a native 466x466
  purpose-made image — no zoom/recolor needed. **Tried the same on the
  Godzilla speedometer (a radar-grid background) and reverted** — its GIF
  frames have an opaque black backdrop baked into the artwork itself, so
  anything underneath is invisible no matter the z-order. Don't retry this
  without first checking whether the GIF assets have real transparency.
- **Color scheme**: there were *four* independent, inconsistent ad hoc
  color systems across the boot-sweep animation (zones at 3500/6500rpm),
  the live-update path (zones at 1000/3000/6000rpm), a generic
  blue/green/red percent gradient (`get_dynamic_color`, gauge arcs), and
  hardcoded tick colors (redline at 6000rpm). Consolidated into one
  **amber `#FFB020` / hot `#FFC54D` / redline `#FF3B1D`** scheme at
  consistent 3000/6000rpm-equivalent thresholds — this is now the
  established palette, reuse it for any future gauge/dial work rather than
  introducing another ad hoc set. Speed *and* rpm digit readouts both track
  the zone color now (matching the arc); unit captions ("rpm"/"km/h") stay
  fixed amber.
- **Long-press settings menu** (`open_speedo_settings_menu` in
  `godzilla_speedo_ui.h`, shared by both speedometer screens): replaced ad
  hoc blue/green/red buttons with Settings-style dark cards; replaced a
  "Simulate OBD ON/OFF" button whose own label doubled as an ambiguous
  status readout with a real switch. Found two real LVGL bugs while
  redesigning, now fixed: the flex container was left scrollable with
  default padding (stray horizontal scrollbar), and the close button was
  never moved to the foreground, so the container created after it (their
  bounds overlapped near the top) silently ate clicks in that band.

**Verification note specific to this UI work:** the real first-run
onboarding WiFi overlay (2s after boot, real firmware behavior) covers
*any* forced screen almost immediately, including ones reached via
`EMU_FORCE_SCREEN`. There's no clean way to screenshot around it without
either suppressing real behavior (rejected — see process notes) or a real
simulated click on its close button (fiddly to get right via AppleScript
window-geometry math, and the user generally preferred to check visually
themselves rather than wait on that). Default to asking the user to verify
when the overlay would be in the way, rather than spending tool calls
fighting click coordinates.

## Notes / decisions
- This lives in `emulator/` at the repo root (not nested in the sketch
  folder), to make the "reusable, not Trailmaster-specific" intent visible
  in the layout.
- The existing `sim/` (WASM, per-screen extraction) stays as-is for now —
  not deleted. Once `emulator/` can render the real app end-to-end, decide
  whether to retire `sim/` or keep both (sim/ is faster to iterate visuals
  with `Module.ccall`; emulator/ is the source of truth for correctness).
- **Image asset conversion (PNG -> LVGL `lv_img_dsc_t`)**: no converter
  tool was on hand, so a one-off Python/PIL script did it — read the PNG,
  pack each pixel as RGB565 little-endian (2 bytes) + alpha (1 byte) to
  match this project's `LV_IMG_CF_TRUE_COLOR_ALPHA`/`LV_COLOR_16_SWAP=0`
  config (verified by checking `lv_conf.h` and reverse-engineering an
  existing asset's byte count against its known w*h), then emit the same
  `const uint8_t ..._data[]` + `const lv_img_dsc_t` structure SquareLine's
  own generated files use. The script isn't checked in anywhere (it lived
  in the session's scratchpad) — recreate it if another image needs
  converting rather than searching for it.

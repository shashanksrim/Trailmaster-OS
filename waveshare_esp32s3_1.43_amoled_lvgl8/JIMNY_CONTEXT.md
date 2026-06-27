# Jimny Dashboard & Emulator Context Log

This file serves as a persistent context store for the development of the Jimny Dashboard and the integrated NES emulator on the Waveshare ESP32-S3 1.43" AMOLED.

---

## ARCHITECTURE OVERVIEW

```
loop()  (Core 0)
  ├─ lv_timer_handler()          ← LVGL render + event dispatch
  ├─ Screen detection block      ← Detects active screen, routes updates
  │    ├─ ui_uiinclinometer  →   update_screen_inclinometer()
  │    ├─ ui_uiScreenGame    →   update_screen_game() (Dino)
  │    └─ ui_uispeedometer   →   update_screen_ui() (OBD data)
  └─ Emulator yield (delay 50ms when MODE_EMULATOR)

nesTask() (Core 1)
  └─ NES emulator main loop (nofrendo)
       └─ controller.cpp polls getTouch() for swipe-down exit
```

**CRITICAL RULE**: When the emulator runs (MODE_EMULATOR), `loop()` on Core 0 only calls `delay(50)`. It does NOT touch LVGL, I2C, or SPI. The emulator on Core 1 exclusively controls the display and touch. Any violation causes I2C bus collisions and freezes.

---

## 1. Emulator Ignition & Controller Logic
*   **Problem:** Games stuck in demo mode; ESP32 crashed on exit; swiping down froze the board.
*   **Root Cause:**
    *   `atexit()` never fires in FreeRTOS → `esp_timer` leaked across launches → game ran at 2x–3x speed.
    *   Static controller vars (`ignition_triggered`) not reset between game launches.
    *   Core 0 and Core 1 both calling `getTouch()` concurrently → I2C bus collision.
*   **Solution:**
    *   Call `shutdown_everything()` explicitly before `main_loop()` returns in `nofrendo.c`.
    *   Call `reset_nes_controller()` in `NesEngine::loadROM()` before every launch.
    *   `loop()` yields (`delay(50)`) while emulator runs. Core 1 signals exit via `NesEngine::is_running = false`.

## 2. LVGL Touch Driver & UI Freezes
*   **Problem:** Speedometer UI unresponsive; swipe-down launcher wouldn't appear.
*   **Root Cause:** `my_touch_read()` returned early after detecting a swipe without setting `LV_INDEV_STATE_REL`, making LVGL think the screen was permanently touched.
*   **Solution:** Always set `data->state = LV_INDEV_STATE_REL` before returning after a swipe intercept.

## 3. Photoframe Background Flicker
*   **Problem:** Screen flashed every 5 seconds while on game menu.
*   **Solution:** Wrapped `photoframe_loop_handler()` in `if (currentMode == MODE_PHOTOFRAME)`.

## 4. Premature Game Ignition
*   **Problem:** Game went into demo mode because auto-ignition fired at `elapsed = 0ms`.
*   **Root Cause:** Finger still on screen from tapping the menu. Game title screen takes 1.5–2s to start listening for input.
*   **Solution:** Track `initial_touch_released` flag. Enforce a minimum 1.5s boot delay. Extended auto-ignition fallback to 2.5s.

## 5. Unthrottled Game Speed on Relaunch
*   **Problem:** Emulator ran at 2x–3x speed after first launch.
*   **Root Cause:** `atexit()` registered `shutdown_everything()` but never triggered in FreeRTOS. Multiple `esp_timer` instances accumulated.
*   **Solution:** Explicit `shutdown_everything()` call immediately before `main_loop()` returns in `nofrendo.c`.

## 6. The Phantom Click (Swipe to Exit)
*   **Problem:** Swiping down in game menu accidentally launched Dino Game.
*   **Root Cause:** After swipe intercept, finger still on screen, next cycle sent `LV_INDEV_STATE_PR` on button underneath.
*   **Solution:** `ignore_until_lift` flag in `my_touch_read()`. All touch data swallowed until hardware reports finger lifted (100ms timeout after last touch).

## 7. Premature Ignition (Video Sync Bug)
*   **Problem:** Ignition timer bypassed title screen correctly on first launch but failed on subsequent ones.
*   **Root Cause:** Timer started before PPU rendered first frame. Variables cached in Core 1 CPU registers, blinded to Core 0 resets.
*   **Solution:** Marked all controller state `volatile`. Moved timer start to `nes_notify_first_frame_rendered()` called from `vid_flush()` in `osd.cpp`.

## 8. Dead Controller (No D-Pad Input)
*   **Problem:** Virtual auto-ignition and touch steering ignored by NES games.
*   **Root Cause:** `osd_init()` in `osd.cpp` was empty — `joypad_p1` was declared but never registered with nofrendo core.
*   **Solution:** Initialized and registered `joypad_p1`/`joypad_p2` inside `osd_init()`. Implemented `input_shutdown()` in `nesinput.c` to clear active input array on each game exit.

## 9. Uninitialized LVGL Pointer Crash on Swipe
*   **Problem:** Swiping down on Speedometer/ROM Menu crashed the system.
*   **Root Cause:** `my_touch_read()` returned `LV_INDEV_STATE_REL` early without setting `data->point`. LVGL processed garbage coordinates (e.g., 49283, -18239) → fatal pointer exception.
*   **Solution:** Always populate `data->point` with physical hardware coordinates immediately after `getTouch()` returns true.

## 10. ROM Menu Phantom Clicks & Gatling Gun Ignition
*   **Problem 1:** Swiping down on ROM menu accidentally clicked Dino Game button.
*   **Solution 1:** Added `if (ignore_until_lift) return;` to all game launch event callbacks.
*   **Problem 2:** 1.2s auto-ignition insufficient for long-title games (Flappy Bird).
*   **Solution 2:** Adaptive "Gatling Gun" sequence: `START → START → A → START`. Flappy Bird: 6.0s. All others: 1.5s.

## 11. Graphical ROM Gallery & Swipe Gesture Isolation
*   **Problem 1:** Swiping down inside active NES game caused watchdog restart.
*   **Root Cause:** `is_running = false` let LVGL (Core 0) send SPI commands while emulator (Core 1) was still rendering → SPI collision.
*   **Solution 1:** Swipe down now calls `nes_poweroff()` for graceful emulator shutdown.
*   **Problem 2:** ROM gallery ordering inconsistent.
*   **Solution 2:** Load SD card directory into array, iterate with a `get_prio()` filter to build cards in exact sequence: Flappy → Dino → Galaga → Road Fighter.
*   **Problem 3:** Swipe-down from ROM menu was hijacking `ui_uilauncher` screen object.
*   **Solution 3:** ROM menu now lives on its own dedicated `rom_screen` object. Launcher remains intact.
*   **Custom BMP Loader:** `load_bmp_to_psram()` manually parses BMP headers, converts BGR→RGB565, injects directly into PSRAM as native `lv_img_dsc_t`. Bypasses LVGL's broken BMP filesystem support.

## 12. Launcher Freeze (LVGL Scroll Event Infinite Loop)
*   **Problem:** Jimny Launcher became frozen and unresponsive after swiping.
*   **Root Cause:** The `launcher_overscroll_cb` was registered on `LV_EVENT_SCROLL` and checked `scroll_y < -50`. Because `LV_EVENT_SCROLL` fires on every tick while pulled down, and the callback kept calling `exit_launcher()` which restarted the fade animation, LVGL entered an infinite animation-restart loop that choked the CPU.
*   **Solution:** Reverted to `LV_EVENT_ALL` listener. The callback checks for `LV_EVENT_GESTURE` + `LV_DIR_TOP` (swipe up), fired only once per gesture. Secondary swipe-up guard added in `my_touch_read()` checking `touch_start_y - y > 100` when on the launcher.

## 13. Ghost Touches & IMU Car Rotation (I2C Uninitialized Memory)
*   **Problem:** Ghost touches froze the launcher; inclinometer car rotated wildly on entry; tap-to-reset didn't work.
*   **Root Cause:** `FT3168.cpp` `getTouch()` and `qmi8658c.cpp` `qmi8658_read_sensor_data()` both used **uninitialized stack buffers** when I2C reads failed (dropped packet). Garbage memory was interpreted as valid touch coordinates (clamped to 466,466) and as acceleration data (millions of G-forces).
*   **Solution:**
    *   `FT3168.cpp`: Initialize `data=0; buf[4]={0}`. Check `I2C_read_buff() == ESP_OK` before using data.
    *   `qmi8658c.cpp`: Initialize `buf_reg[12]={0}` and `status=0` before use.

## 14. IMU Sensors Running When Not on Inclinometer Screen
*   **Problem:** After visiting the inclinometer once, `qmi8658_enableSensors(QMI8658_ACCGYR_ENABLE)` was never reversed. Sensor hardware kept sampling at 250Hz on the shared I2C bus even on unrelated screens.
*   **Solution:** In `loop()`, the screen-change detector now explicitly calls `qmi8658_enableSensors(QMI8658_DISABLE_ALL)` and resets `imu_ready = false` the moment the active screen changes away from `ui_uiinclinometer`. Sensors are re-initialized on the next inclinometer visit.

## 15. Jimny Launcher — Snap-Scroll & Orange Selection Bar
*   **Feature:** Launcher `ui_Panel1` now uses `LV_SCROLL_SNAP_CENTER` so every swipe lands a menu item exactly in the center. A semi-transparent orange bar (opaque border, orange fill at 35% alpha) sits at the Panel's vertical center on the `ui_uilauncher` layer — it does NOT scroll.
*   **Key Design Decisions:**
    *   All six buttons inside Panel1 have `LV_OBJ_FLAG_CLICKABLE` cleared. Direct taps on buttons do nothing.
    *   The orange bar is the **only** clickable element. On tap, it reads `lv_obj_get_scroll_y(ui_Panel1)` to find which child button's center is within ±45px of the panel center, then dispatches the correct action.
    *   Symmetric padding added: `pad_top = pad_bottom = 124px` so the first and last items can scroll to center. Formula: `(panel_height - button_height) / 2 = (331 - 83) / 2 = 124`.
    *   Button order in Panel1 (index 0–5): Speedometer, Inclinometer, Gauges, ImageFrame, Games, Settings.

---

## ROM MENU LAYOUT (build_rom_menu)
*   Uses `lv_obj_set_flex_align(rom_list, LV_FLEX_ALIGN_START, ...)` (NOT CENTER) so the first card starts at the left edge, pushed to center by 123px left padding. `LV_FLEX_ALIGN_CENTER` caused the whole card block to be centered, landing between cards on initial load.
*   Card priority order: `get_prio()` returns 1=Flappy, 3=Galaga, 4=Road Fighter, 10=others. Dino card is always inserted at position 2 (after Flappy).

---

## FUTURE DEVELOPMENT RULES

> **NEVER** poll I2C or SPI concurrently from both cores without a mutex.

> **ALWAYS** wrap C++ headers in `extern "C"` when called from the nofrendo core (plain C).

> **NEVER** place `extern "C"` declarations inside C++ function bodies. Always at global/file scope. Causes: `expected unqualified-id before string constant`.

> **ALWAYS** reset static state variables in "No-Reboot" flows.

> **LVGL State:** Every `LV_INDEV_STATE_PR` must be matched with a `LV_INDEV_STATE_REL`.

> **LVGL Scroll Events:** Never call `lv_scr_load_anim()` inside an `LV_EVENT_SCROLL` callback — it creates an infinite animation-restart loop that freezes the CPU.

> **ui_uilauncher.c** is SquareLine-generated. Do NOT add permanent logic here. Post-init customisation (snap, orange bar, event guards) belongs in `setup()` in the main `.ino` file, called AFTER `ui_init()`.

> **IMU Init Cost:** `qmi8658_init()` → `qmi8658_get_id()` → `qmi8658_on_demand_cali()` blocks the I2C bus for ~2.3 seconds. Only call this once. Use `imu_ready` flag. Disable sensors (`QMI8658_DISABLE_ALL`) when leaving the inclinometer to prevent background I2C traffic.

> **Memory:** The binary is near the flash partition limit. Every new feature must be lightweight. Moving fonts/images to SD card (loaded at runtime via `load_bmp_to_psram`) is preferred over embedding in flash.

## 16. Launcher — Orange Bar Polish

*   **Touch Area**: Reduced to a transparent `click_zone` child (405x73px) matching button width. Bar itself has `LV_OBJ_FLAG_CLICKABLE` cleared.
*   **Border Fading**: Fixed with `border_width=3`, `border_opa=255`, `outline_width=0`.
*   **One-item-per-swipe**: `indev_drv.scroll_throw = 3` (default 10) for tight magnetic snapping.
*   **Panel1 centering**: SquareLine `y=71` shifted Panel1 to screen y=304. Overridden with `lv_obj_set_y(ui_Panel1, 0)` so both bar and panel center at screen y=233.
*   **Detection formula**: `abs(ch_y - scroll_y) < 45` — ch_y is content-area relative, item i is centered when scroll_y == ch_y.

## 17. Flappy Bird — Auto-Ignition Timing Fix

*   **Problem**: Flappy Bird takes 7-8s to render its interactive title. Old code fired Gatling after 1.2s + 6s window — both expired before the game was ready.
*   **Solution**: `is_flappy = strcasestr(current_rom_path, "flappy")`. Ignition delay = 8500ms, Gatling window = 10000ms for Flappy; 1200ms/1500ms for others.
*   **Why**: `game_launch_ms` resets at first frame (early VRAM init), not at interactive title. 8.5s delay aligns Gatling with actual title screen readiness.

## 18. Settings Screen

*   **Brightness row**: Label "Brightness" in `ui_font_rajdhani1` with − / + buttons (orange circles). Values 1–10 mapped to register 0x51 (WRDISBV): level × 25 = brightness byte (25–255). Default = 8. `Amoled::io_handle` added as class member and stored during `begin()` so `setBrightness()` can send the QSPI command after init.
*   **Bootloader row**: Label "Bootloader" with a CLEAR button. Deletes `/sd_card/boot_img.txt` — the config file written by the Photo Frame app's "SET AS BOOTLOADER" long-press dialog. Removing this file reverts the boot splash to the built-in Jimny logo. Button turns green ("CLEARED") on success, grey ("NOT SET") if no custom splash was configured.
*   **Navigation**: Swipe-down returns to launcher via `switch_to_launcher()`.
*   **How the bootloader system works**: `show_boot_splash()` in `PhotoFrameApp.cpp` reads `/sd_card/boot_img.txt`, finds the path to a `.bin` (466x466 RGB565) file on the SD card, and displays it on boot. If the file does not exist, the default `Jimnylogo` embedded image is shown instead.

## 19. Inclinometer Stabilization & Settings Toggle

*   **Inclinometer Startup Settle**: Implemented `imu_settle_count` skipping filter logic for first 30 frames in `update_imu_math()` upon screen entry, forcing it direct-to-accelerometer to eliminate the high-speed integrate spinning surge.
*   **Tap to Reset**: Modified `load_inclinometer_mode()` to flag `ui_uiinclinometer` as clickable and attached a global screen click handler set `pitch_offset = current_pitch; roll_offset = current_roll`. Added small footer label "Tap screen to zero reset" at the screen bottom.
*   **Settings Flex Fix**: Refactored Brightness controls into an invisible sub-container (`bright_cont`) using `LV_FLEX_FLOW_ROW` layout. This perfectly aligns the minus, value, and plus horizontally and equidistant.
*   **Bootloader Toggle Switch**: Replaced "CLEAR" button with `lv_switch_t`. On bootloader toggle:
    - Reads initial condition via POSIX `access("/sd_card/boot_img.txt", F_OK)`.
    - Turning OFF executes `rename(boot_img.txt, boot_img.txt.disabled)` preserving user configuration but hiding it from loader.
    - Turning ON executes `rename(boot_img.txt.disabled, boot_img.txt)` allowing fast reuse of configured bootsplash images without reuploading from photo sync app.

## 20. Flappy Bird Gameplay & UX Finalization

*   **Flappy Tap Mapping**: Modified `controller.cpp` to translate real-time touch detection directly into the `HW_MASK_A` gamepad command. User taps now perform distinct flaps/jumps.
*   **Auto-Gas Override**: Disabled the passive hold on the A-button exclusively for Flappy Bird ROMs so user release/press rhythm is properly ingested by game logic.
*   **Ignition Window**: Reduced the Gatling ignition injection window to 8 seconds exactly to shorten wait times while preserving boot readiness.
*   **Typography Polishing**: Increased inclinometer zero-footprint label from Montserrat 14 to Montserrat 16, text refined to "Tap to reset". Updated system settings nomenclature from "Bootloader Logo" to "Boot image".

## 21. Roll Arc Gauges on Inclinometer Screen

*   Two LVGL arc widgets (`g_left_arc`, `g_right_arc`) built once via `build_roll_arcs()` on first entry.
*   **Left arc**: bg_angles 140°–220° (80° span centred at 180° = left edge). Maps roll via LV_ARC_MODE_SYMMETRICAL.
*   **Right arc**: bg_angles 320°–40° (centred at 0° = right edge). Same value, mirrored.
*   **Colour ramp**: `danger_color()` interpolates white→red as |roll| goes from 0 to 35° (MAX_SAFE_DEG). Clamped at 40° (MAX_DISP_DEG).
*   **Triangle pointer** labels (▶ left, ◀ right) repositioned each frame using `cosf/sinf` at PTR_R=195px from centre.
*   **Tick labels** at ±10, ±20, ±30, ±40 positioned at LABEL_R=175px. 30° and 40° rendered in red, rest in grey.
*   **+ / ─** blue centre-axis markers added at the horizontal midpoints of each arc.
*   `update_arc_gauges(display_roll)` called every 25 ms inside existing `update_screen_inclinometer()`.

## 22. Inclinometer UI Polishing & Stabilization

*   **Tick Label Alignment**: Moved the angle graduations radially outwards (`LABEL_R` 158 → 188) to position them immediately flush with the arc gauge track, maximizing visibility while maintaining logo clearance.
*   **Full Loop Fusion (Stability Fix)**: Rolled back the fast-alpha pure accelerometer path for the arc updates. The arc now utilizes the exact same 0.96 complementary filter output used for numerical readout, ensuring perfectly smooth, drift-free motion unaffected by slight chassis vibration twitch.
*   **Arc Visual Physics**: Validated mirrored fill behaviors such that a tilt causes both gauge tracks and their respective triangle pointers to drift identically relative to physical horizon, perfectly simulating physical instrument roll physics.

## 23. Launch Versioning & Automated Reset Handling

*   **FIRMWARE VERSION INJECTION**: Defined global `JIMNY_FW_VERSION "v2.3.23"` aligned with master Context Revision 23. Injected programmatic label override in `setup()` so the Launcher natively prints correct current iteration on physical hardware without SquareLine re-exports.
*   **AUTOMATIC 1.0s CALIBRATION**: Implemented `auto_reset_triggered` semaphore management. Every time the Inclinometer screen loads, a 1.0s timer triggers once, executing a hardware level snap-to-zero to wipe away persistent accelerometer launch transients and initial inverted vectors perfectly.
*   **RADIAL GRADUATION SPACINGS**: Relocated Tick Label radius (`LABEL_R`) to exact optimized threshold `176`. This successfully expands the spacing visually by exactly providing perfect contrast separation from the Arc Gauge without intersecting dashboard logos.

## 24. Speedometer Dashboard Custom Expansion

*   **Dynamic Multi-Range RPM Arc**: Designed visual extension hook in `screen_ui.h` (`build_speedo_rpm_gauge`). Unlocks standard SquareLine template arc to project full range `0-8000 RPM` at 12px weighting across 270° span.
*   **Radial High-Fidelity Tick Generation**: Algorithmically spawns 9 numeric markers (0-8) and 9 parallel visual anchor lines along outer rim radius. 
*   **Engine Load Color Palette Synthesis**: Ticks and the active gauge dynamically adjust state across customized visual zones:
    *   `0-3500`: Dynamic Electric Yellow
    *   `3500-5500`: Core Cruise Blue
    *   `5500-6500`: Peak Pull Orange
    *   `6500-8000`: Maximum Hazard Red

## 25. High-Fidelity Speedometer Graduation System

*   **White Graduation Geometry**: Overhauled `build_speedo_rpm_gauge` to generate static pure-white instrument markings. Replaced the solid background arc with a high-density 41-element grid system comprising:
    *   **9 Bold Major Ticks** at every 1,000 RPM with large, highly legible numeric overlays.
    *   **32 Subordinate Minor Ticks** every 200 RPM for an authentic mechanical dashboard feel.
*   **Optimized Color Sequence**: Consolidated RPM active arc sweeps to strict tri-band spectrum (`Yellow <3500`, `Electric Blue <6500`, `Red 6500+`) matching standard track-grade readability.

## 26. Edge-To-Edge Perimeter Overhaul (Dashboard)

*   **Glass Perimeter Binding**: Pushed all circular overlays outward from R=212 down to pure hardware radius `R=232`. Gauge strictly maps to physical border glass.
*   **High-Contrast Tick Beefing**: Inflated visual densities for instantaneous comprehension. Secondary ticks are now robust `4px`, and Primary major ticks are anchor-level `6px` with length stretched nicely to fill new gap.
*   **Expanded Inset Buffer**: Pushed numerical ring deeper to `R_INNER=170`, giving heavy weight tick graphics extreme breathing room from typography.

## 27. Dashboard Final Typography & Redline Safe-Zone

*   **Redline Alert Sector**: Spawned dynamic static foreground arc tracked precisely between `338° and 45°` (radial coverage for 6k-8k RPM zone). Designed as 5px crisp-transparent accent line tucked tight behind scale lines for maximum performance look.
*   **Industrial Font Swap**: Cut legacy Montserrat in favor of Project-Imported `ui_font_rajdhani1` (32px line height). This entirely solves numeric compression artifacting, forcing perfectly clean, hard-cornered digital 5s and robust instrument readability.

## 28. GIF Bootloader Engine & Persistent Duration Logic

*   **Native GIF Library Unlock**: Set `LV_USE_GIF 1` in core `lv_conf.h`, enabling hardware native .gif loading capability. Integrated the external GIF driver via LVGL virtual file handler mapped directly to POSIX mount point (`S:/sd_card/splash.gif`).
*   **Smart Dispatching Splash System**: Overhauled `show_boot_splash` to detect and run preferred format sequence. Prefers Custom Bitmap (if configured), otherwise falls through to decode `/sd_card/splash.gif` (default), with finalized hard-fallback to standard system logo.
*   **Tunable Delay Setting**: Injected flexible `custom_boot_time` preference persisted to flash storage (`boot_time.txt`). Re-architected Settings screen to host a new interactive controls row permitting dynamic adjustment between 1 and 5 seconds duration.

## 29. Heavy-Duty GIF Direct-to-PSRAM Engine & Universal Scanner

*   **Memory-Mapped Loading**: Abandoned the file-path reference to eliminate file system driver routing instabilities. Replaced with robust C++ `fread()` block-loading which pipes raw GIF binaries directly into dedicated `MALLOC_CAP_SPIRAM` buffer segments for native decoder ingestion.
*   **Omni-Scanner Loop**: Engineered automatic recursive directory snooping logic to the boot dispatcher. If the default `splash.gif` fails presence checks, the system autonomously probes the whole root directory and auto-locks onto the very first `.gif` file candidate detected as an immediate replacement.
*   **Settings Label Alignment**: Adjusted Setting Row 3 text identifier to strictly reflect the branding demand: "**Boot timer**". Unified this global scalar value to rigorously drive the entire splash freeze timeline whether viewing dynamic GIFs, custom bitmaps, or core failsafe graphics.

## 30. Resolving Critical SRAM Overruns in GIF Decoding Lifecycle

*   **Diagnosis of Null Allocation Fail**: Determined that large-screen (466x466) GIFs require nearly 850KB of contiguous working memory for the frame canvas. By default, the third-party `gifdec.c` library routed this request into LVGL Internal RAM pool (`lv_mem_alloc`) which immediately triggers exhaustion & silent component nullification, creating flash-to-blank symptoms.
*   **Decoder Pool Deflection**: Inserted scoped preprocessor macros inside the `gifdec.c` library translation unit to intercept ALL internal allocation traffic. Explicitly undefinded native `lv_mem` hooks and strictly redirected them to `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` on targeting ESP platforms, flawlessly dumping heavy runtime canvases into the expansive external 8MB PSRAM storage.

## 28. Default GIF Bootloader Integration

*   **Native GIF Decoder Enabled**: Activated `LV_USE_GIF` in `lv_conf.h` to allow native decoding and playback of animated GIFs within the LVGL environment.
*   **Dynamic Boot Splash Selection**: Updated `show_boot_splash()` in `PhotoFrameApp.cpp`. The system now prioritizes a custom bootloader image (`.bin`). If none is set, it checks for `splash.gif` on the SD card root and plays it for 6 seconds. If neither exists, it gracefully falls back to the static Jimny logo for 3 seconds.

## 29. GIF Hardware Memory Decoding Fix

*   **LVGL Memory Architecture Switched**: Converted `LV_USE_STDLIB_MALLOC` from the default `LV_STDLIB_BUILTIN` (which caps LVGL at a static 64KB internal heap pool) to `LV_STDLIB_CLIB`. This seamlessly maps standard `malloc()` calls so LVGL can directly utilize the ESP32-S3 external PSRAM. This entirely resolves the silent black-screen crash when attempting to load the massive 466x466 `splash.gif` into memory.

## 30. GIF Decoder Diagnostics

*   **Telemetry Injection**: Inserted detailed `Serial.printf` debug trace points into the boot sequence within `PhotoFrameApp.cpp` to monitor the exact execution state, filesystem mounting status, and decoder engagement of the GIF engine.

## 31. GIF Decoder Tick and Filesystem Fix

*   **Tick Engine Unfrozen**: Injected `lv_tick_inc()` into the `show_boot_splash` wait loop. The GIF decoder animation timer was previously frozen because the `setup()` block preempted the main `loop()` where the LVGL tick clock normally advances.
*   **Raw Memory Bypass for LVGL POSIX**: Directly loaded the `.gif` file from the hardware SD driver straight into PSRAM, bypassing LVGL`s POSIX filesystem driver (which had failed to mount during `setup()`). The raw memory struct `&img_dsc` is now fed directly to `lv_gif_set_src`, resulting in flawless and instant playback.

## 32. GIF Decoder Dynamic PSRAM Allocation

*   **Dynamic GIF Memory Scaling**: Refactored the raw memory bypass loader in `show_boot_splash()`. The system now dynamically allocates a custom PSRAM buffer scaled exactly to the physical `.gif` file size (`st.st_size`), rather than erroneously constraining it to the static 434KB `psram_buffer` meant exclusively for uncompressed RGB565 `.bin` streams. It securely frees the buffer and resets the global `img_dsc` state after playback, entirely preventing memory exhaustion crashes for large multi-megabyte GIF files.

## 33. LVGL v8 Core Memory Manager Override

*   **Forced PSRAM Core Allocations**: Discovered that the `lv_conf.h` file contained mixed v9 tags, causing previous `LV_USE_STDLIB_MALLOC` PSRAM bypass attempts to fail silently. Completely replaced the v9 memory block with legacy v8 `LV_MEM_CUSTOM` macros explicitly bound to `heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`. This successfully forces the core LVGL memory engine (which processes the massive 868KB `gif_open` canvases) out of the limited 64KB internal RAM and into the 8MB external PSRAM.

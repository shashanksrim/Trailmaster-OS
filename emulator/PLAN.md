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
| L2 | Arduino-ESP32 core shim: `Serial`, `millis`/`delay`, `Preferences` done. **Still needed**: `WiFi`, `SD`/`FFat` (path-redirect `/sd_card` → local `sd_files/`), FreeRTOS task fns, `HTTPClient`/`Update` | any ESP32 Arduino sketch | partial (`emulator/core/`) |
| L3 | Runtime bootstrap: call `setup()` once, `loop()` forever, pump SDL/LVGL events between | any program | **done & verified end-to-end** (`emulator/runtime/main.cpp`) — native SDL2 (brew) installed; trivial test sketch compiled+ran as a real macOS window, screenshot-confirmed correct rendering through the full real-Amoled-class pipeline |
| L4 | The actual sketch (`.ino` + supporting files) | swappable per project | n/a — compiled as-is, zero edits |

**Emulator = L0+L1+L2+L3.** L4 is a `--sketch <path>` argument, not baked in.

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

## Notes / decisions
- This lives in `emulator/` at the repo root (not nested in the sketch
  folder), to make the "reusable, not Trailmaster-specific" intent visible
  in the layout.
- The existing `sim/` (WASM, per-screen extraction) stays as-is for now —
  not deleted. Once `emulator/` can render the real app end-to-end, decide
  whether to retire `sim/` or keep both (sim/ is faster to iterate visuals
  with `Module.ccall`; emulator/ is the source of truth for correctness).

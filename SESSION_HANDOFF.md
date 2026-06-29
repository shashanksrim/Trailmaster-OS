# Session Handoff — read this first in a fresh session

Written because the previous session hit ~85% context. Everything below is
committed to git — nothing is lost by starting fresh. This file orients you;
`emulator/PLAN.md` has the full technical detail for the active workstream.

## What Trailmaster-OS is
A custom firmware ("Jimny Dash" / "Trailmaster") for the Waveshare ESP32-S3
1.43" round AMOLED touch display, mounted in a Suzuki Jimny. Reads live OBD-II
data over WiFi (ELM327 adapter), plus speedometer/gauges/inclinometer/photo
frame/NES games/settings screens. Built on Arduino + LVGL 8.3.11.

## Two-workspace setup — IMPORTANT, read before touching anything
There are **two folders, two git branches, sharing one `.git` via `git worktree`**:

| Folder | Branch | Purpose | Rule |
|---|---|---|---|
| `~/Documents/GitHub/Trailmaster-OS` | `main` | Always == what's published/live on OTA | **Keep pristine.** Only touch this folder for an actual urgent OTA release (`./publish.sh`). Never do exploratory/dev work here. |
| `~/Documents/GitHub/Trailmaster-OS-dev` | `sim-dev` | All active development (the emulator project) | This is where you resume work. |

This split exists because a previous session accidentally risked mixing WIP
with what's live. Verify state before doing anything:
```bash
cd ~/Documents/GitHub/Trailmaster-OS && git status --short && git log --oneline -1
# should show: clean, HEAD at the latest "Release vX.X" commit

cd ~/Documents/GitHub/Trailmaster-OS-dev && git status --short && git log --oneline -3
# should show: clean, HEAD = the latest emulator commit
```
If `main` isn't clean, STOP and figure out why before doing anything else.

## OTA system — DONE, working, not what we're actively working on
Firmware OTA + SD-file OTA + a browser USB flasher (GitHub Pages) are all
built, tested, and live at github.com/shashanksrim-sys/Trailmaster-OS
(currently v3.8). Release flow: bump `APP_VERSION` in `version.h`, build,
`./publish.sh [--with-sd] <bin> "notes"`. See `MEMORY.md`-equivalent context
in `waveshare_esp32s3_1.43_amoled_lvgl8/test/` and earlier conversation if
you need this — it's stable and not the current focus.

## CURRENT ACTIVE WORK: the reusable AMOLED board emulator
**Read `emulator/PLAN.md` in full — it has the complete layered architecture,
day-by-day log, every gotcha found, and exact next steps.** Summary:

**Goal:** a standalone emulator for the Waveshare ESP32-S3 1.43" AMOLED board
that runs the REAL, unmodified firmware natively on a Mac (not a hand-rebuilt
approximation) — so any screen/feature renders correctly without per-screen
manual porting work, and so it's reusable for other sketches on this board.

**Status: major milestone reached.** The real `.ino` (with exactly one
mechanical, build-time-only line transform — the tracked file is never
edited) compiles, links, and runs as a native macOS app, screenshot-verified
rendering the actual `build_settings_screen()` pixel-correct.

**Run it right now:**
```bash
cd ~/Documents/GitHub/Trailmaster-OS-dev
./emulator/build.sh && ./emulator/build/trailmaster_emulator
```
A real window should open. Bring it forward and screenshot if you need to
verify visually (`osascript ... frontmost ...` then `screencapture -x`).

**Known open item:** it boots to the Settings screen instead of the
speedometer. Not yet root-caused — investigate this first in a fresh
session. Likely something in `setup()`'s default-screen logic interacting
with a stubbed dependency (e.g. `default_speedometer`/Preferences default,
or the onboarding-overlay logic reacting to the stubbed SD/WiFi state).

**What's deliberately stubbed this pass** (see `emulator/board/firmware_stubs.cpp`):
PhotoFrameApp (WiFi portal/photos), OTAManager (real network OTA), GIF/PSRAM
parts of the Godzilla speedometer, NES/SMS game engines. Everything else —
including Dino/Flappy game logic (`screen_game.cpp`) — is the real code.

**Day 3 candidates** (not started): root-cause the boot-screen issue; verify
other screens render (speedometer, gauges, inclinometer, launcher, OTA
overlay) via screenshots; consider trying the real AnimatedGIF library
(it's portable C, might "just work" now that SD/heap_caps are faked).

## How to verify nothing is broken before continuing
```bash
cd ~/Documents/GitHub/Trailmaster-OS-dev/waveshare_esp32s3_1.43_amoled_lvgl8
./test/run_tests.sh                                    # unit tests, should be 30/30
arduino-cli compile --profile amoled --output-dir ./build_out   # real firmware still compiles
cd ~/Documents/GitHub/Trailmaster-OS-dev
./emulator/build.sh                                     # emulator still builds
```

## Style/process notes from this session
- User is budget-conscious about context/tokens — work efficiently, avoid
  re-deriving facts already in `PLAN.md`, commit checkpoints frequently.
- "Zero edits to tracked firmware files" is a hard constraint for the
  emulator work — any transform must be build-time-generated, never applied
  to the files in git.
- Don't use `git add -A` carelessly in the dev workspace without checking —
  it's accidentally tracked `.claude/` (local tooling) twice already; both
  `.gitignore`s now have `.claude/` and `**/.claude/` entries.

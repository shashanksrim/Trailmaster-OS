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

**Status: Day 4 done.** The real `.ino` (with exactly one mechanical,
build-time-only line transform — the tracked file is never edited)
compiles, links, and runs as a native macOS app. Every screen except
NES/SMS and OTAManager's real network OTA now runs real, compiled-as-is
firmware code (see "Day 4" below and in PLAN.md).

**Run it (double-click, no Terminal needed beyond the one-time build):**
```bash
cd ~/Documents/GitHub/Trailmaster-OS-dev
./emulator/build.sh
open "emulator/build/Trailmaster Emulator.app"   # or double-click it in Finder
```
A real window opens, booting through a real **TRAILMASTER splash** into the
**Speedometer** by default. ~2s after boot, the real first-run-onboarding
WiFi overlay opens automatically (since no networks are saved) — that's
real firmware behavior, not an emulator artifact; same as a real fresh
device. To jump straight to a specific screen instead, set
`EMU_FORCE_SCREEN=gauge|inclinometer|launcher|settings|about|godzilla|imageframe`
before running the raw binary (`./emulator/build/trailmaster_emulator`) —
it jumps there right after `setup()`.

To screenshot it: `pkill -f trailmaster_emulator` first if an old instance
might still be running (stale windows have caused false readings before),
run fresh, then `osascript -e 'tell application "System Events" to set
frontmost of process "trailmaster_emulator" to true'` followed by
`screencapture -x <path>`. **If the screenshot shows the macOS desktop/lock
screen instead of the app, the screen is probably actually locked** —
check before assuming a rendering bug.

**Screenshot-verified correct** (all via real firmware code, not hand-rebuilt
approximations): Speedometer, Gauges, Inclinometer, Launcher/grid, Settings,
About, Godzilla speedometer (real GIF/PSRAM playback), Image Frame carousel
(real photo decode — `sd_files/img_trailmaster.bin` renders), WiFi-upload
overlay (QR code, toggle switch, real onboarding trigger), boot splash.

**What's still deliberately stubbed** (see `emulator/board/firmware_stubs.cpp`):
NES/SMS game engines, and OTAManager's real network OTA (real HTTPS calls to
GitHub + ESP32 flash-partition writes — no desktop equivalent worth faking;
user confirmed this boundary explicitly). Everything else — including
Dino/Flappy (`screen_game.cpp`), the Godzilla speedometer, and PhotoFrameApp
(WiFi portal + photo carousel) — is real, compiled-as-is firmware code. See
PLAN.md's "Day 4" section for the two real emulator-side bugs found and
fixed along the way (`sdcard_shim.h` only redirecting `fopen` not
`opendir`/`stat`/`unlink`; setup()-time rendering never reaching the SDL
window).

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
- **Don't special-case emulator behavior to make verification/screenshots
  easier.** An attempt to suppress the first-run-onboarding overlay
  specifically when `EMU_FORCE_SCREEN` was set (so debug screenshots
  wouldn't get covered by it) was explicitly rejected: "we don't want the
  functionality to be suppressed... this is an emulator, we should not
  custom do stuff." The user's stated intent is a generic "drag and drop
  any .ino" emulator — fix real gaps in the generic L1/L2 shim layer (like
  the two Day-4 bugs found in `sdcard_shim.h` and the present/flush
  pipeline), don't add debug-only conditionals that change firmware-visible
  behavior.
- When verifying visually, confirm with the user before repeating
  screenshot attempts — they may already be watching the window themselves
  and can confirm faster than another screenshot round-trip.

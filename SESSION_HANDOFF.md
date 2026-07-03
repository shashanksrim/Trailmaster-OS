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

## CURRENT ACTIVE WORK: using the emulator to iterate on firmware UI
**Read `emulator/PLAN.md` in full — it has the complete layered architecture,
day-by-day log, every gotcha found, and exact next steps.** Summary:

**The emulator itself (Days 1-5) is done and stable** — a standalone build
that runs the REAL, unmodified Trailmaster firmware natively on a Mac, every
screen except NES/SMS and OTAManager's real network OTA running real,
compiled-as-is firmware code. **The active work as of Day 5 has shifted to
using it for its intended purpose: iterating on firmware UI/UX** (Settings
screen, WiFi overlay, speedometer/gauge colors and backgrounds, the
long-press settings menu) with live screenshot verification instead of
flash-and-look. See PLAN.md's "Day 5" section for the full list of what
changed and the workflow that worked (mock up with `mcp__visualize` first,
then implement for real and verify via `EMU_FORCE_SCREEN`).

**Open item for next session:** Dino/Flappy were fixed (RetroEngine
compiled for real, an event-flood performance bug fixed) but never
re-verified visually afterward — the user asked to park it and pivot to UI
work right after the fix landed. Check `EMU_FORCE_SCREEN=dino` or
`=flappy` first thing next session.

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
`EMU_FORCE_SCREEN=gauge|inclinometer|launcher|settings|about|godzilla|imageframe|dino|flappy|speedosettings`
before running the raw binary (`./emulator/build/trailmaster_emulator`) —
it jumps there right after `setup()`. Note the real first-run onboarding
WiFi overlay still opens ~2s later on top of whatever screen you forced
(real firmware behavior, not suppressed) — screenshot before then, or ask
the user to check visually if timing is tight.

To screenshot it: `pkill -f trailmaster_emulator` first if an old instance
might still be running (stale windows have caused false readings before),
run fresh, then `osascript -e 'tell application "System Events" to set
frontmost of process "trailmaster_emulator" to true'` followed by
`screencapture -x <path>`. **If the screenshot shows the macOS desktop/lock
screen instead of the app, the screen is probably actually locked** —
check before assuming a rendering bug.

**Screenshot-verified correct** (all via real firmware code, not hand-rebuilt
approximations): Speedometer, Gauges, Inclinometer, Launcher/grid, Settings
(redesigned Day 5), About, Godzilla speedometer (real GIF/PSRAM playback),
Image Frame carousel (real photo decode), WiFi-upload overlay (redesigned
Day 5), boot splash, the long-press speedo settings menu (redesigned Day 5).
**Not yet re-verified: Dino/Flappy** after Day 5's RetroEngine/performance
fix — see "Open item" above.

**What's still deliberately stubbed** (see `emulator/board/firmware_stubs.cpp`):
only NES/SMS game engines and OTAManager's real network OTA now (real HTTPS
calls to GitHub + ESP32 flash-partition writes — no desktop equivalent
worth faking; user confirmed this boundary explicitly). Everything else —
including Dino/Flappy + RetroEngine (`screen_game.cpp`/`retro_engine.cpp`,
Day 5), the Godzilla speedometer, and PhotoFrameApp (WiFi portal + photo
carousel) — is real, compiled-as-is firmware code. See PLAN.md's "Day 4"
and "Day 5" sections for the emulator-side bugs found and fixed along the
way (`sdcard_shim.h` redirects, setup()-time rendering, the event-flood
present-throttle bug).

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
- **For UI changes: mock up first, implement second.** Use `mcp__visualize`
  to sketch the proposed layout (HTML/SVG at real 466x466 round proportions)
  and get explicit agreement before touching LVGL code — this caught a
  misread instruction early (centered vs. left-aligned section headers) at
  zero implementation cost. Once agreed, edit the real `.ino`/`.cpp`/`.h`
  files directly (firmware UI work is real feature work, not constrained by
  the emulator's "never edit tracked files" build rule) and verify with
  `EMU_FORCE_SCREEN` + a screenshot.
- **The amber/hot/redline color scheme (`#FFB020`/`#FFC54D`/`#FF3B1D`) is
  now established** for speedo/gauge dials and digits — see PLAN.md's Day 5
  section for the four previously-inconsistent systems it replaced. Reuse
  these exact values for any future gauge/dial work instead of picking new
  ad hoc colors.
- Before editing a value that already has a runtime/live-update code path
  (e.g. a label whose color also gets set elsewhere in a `update_screen_*`
  loop), grep for every place that sets it first — this session found
  color logic duplicated across the boot-animation callback, the live OBD
  update loop, AND a shared inline header, all with different thresholds.
  Editing only the obvious site silently leaves the others stale.
- No Python image-to-LVGL converter was available; one was hand-rolled
  against this project's exact `lv_conf.h` settings (`LV_COLOR_DEPTH 16`,
  `LV_COLOR_16_SWAP 0` → RGB565 little-endian + 8-bit alpha per pixel,
  matching `LV_IMG_CF_TRUE_COLOR_ALPHA`). It wasn't checked in (lived in the
  session scratchpad) — recreate it from PLAN.md's Day 5 notes if another
  image needs converting.

#!/usr/bin/env bash
# Build the reusable Waveshare ESP32-S3 1.43" AMOLED emulator with the real
# Trailmaster .ino + supporting files compiled (almost) unmodified.
#
# The ONLY transform applied to the real .ino is generated at build time
# (never edits the tracked file): stripping one redundant local `extern`
# redeclaration that Clang rejects (stricter than the ESP32 GCC toolchain
# about a harmless linkage mismatch — see emulator/PLAN.md).
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKNAME="waveshare_esp32s3_1.43_amoled_lvgl8"
SK="$DIR/../$SKNAME"
LVGL="$SK/libraries/lvgl"
SDCARD_ROOT="${EMU_SDCARD_ROOT:-$DIR/../sd_files}"
OUT="$DIR/build"
GIFVENDOR="$DIR/board/vendor/AnimatedGIF"
mkdir -p "$OUT"

INC=(-I "$DIR/core" -I "$DIR/board" -I "$DIR/board/fake_esp_idf" -I "$GIFVENDOR" -I "$SK" -I "$LVGL" -I "$LVGL/src")
# Force-include Arduino.h into every C++ TU, matching real arduino-cli's
# behavior of making Serial/millis/etc. ambiently available sketch-wide —
# not just to files that explicitly #include it. Without this, files that
# only get Arduino.h transitively on real hardware (e.g. via AnimatedGIF.h,
# which skips that include on a host/__MACH__ build, by design, since plain
# desktop use of the library shouldn't pull in Arduino-isms) lose access to
# Serial etc. here.
CXXINC=("${INC[@]}" -include "$DIR/core/Arduino.h")

# Generate the build-time sketch copy (see header comment above).
sed '1790s/.*extern bool dino_ready;.*/                \/\/ (emulator build: redundant extern stripped, see emulator\/PLAN.md)/' \
  "$SK/$SKNAME.ino" > "$OUT/sketch_main.cpp"

LVGL_SRC=$(find "$LVGL/src" -name '*.c')

echo "==> Compiling C++ sources..."
g++ -std=c++17 -O1 $(pkg-config --cflags sdl2) \
  "${CXXINC[@]}" \
  -DLV_CONF_INCLUDE_SIMPLE -DEMU_SDCARD_ROOT="\"$SDCARD_ROOT\"" \
  -c "$OUT/sketch_main.cpp" -o "$OUT/sketch_main.o"
g++ -std=c++17 -O1 $(pkg-config --cflags sdl2) \
  "${CXXINC[@]}" \
  -DLV_CONF_INCLUDE_SIMPLE -DEMU_SDCARD_ROOT="\"$SDCARD_ROOT\"" \
  -c "$DIR/core/Arduino.cpp" -o "$OUT/Arduino.o"
g++ -std=c++17 -O1 $(pkg-config --cflags sdl2) \
  "${CXXINC[@]}" \
  -DLV_CONF_INCLUDE_SIMPLE \
  -c "$DIR/runtime/main.cpp" -o "$OUT/main.o"
for f in amoled_sim ft3168_sim qmi8658c_sim firmware_stubs; do
  g++ -std=c++17 -O1 \
    "${CXXINC[@]}" \
    -DLV_CONF_INCLUDE_SIMPLE -DEMU_SDCARD_ROOT="\"$SDCARD_ROOT\"" \
    -c "$DIR/board/$f.cpp" -o "$OUT/$f.o"
done
# Vendored real AnimatedGIF library (portable C/C++, no ESP-specific code —
# see emulator/board/vendor/AnimatedGIF/, copied from the Arduino library so
# Godzilla speedometer's and PhotoFrameApp's GIF playback is real, not stubbed).
g++ -std=c++17 -O1 \
  "${CXXINC[@]}" \
  -DLV_CONF_INCLUDE_SIMPLE \
  -c "$GIFVENDOR/AnimatedGIF.cpp" -o "$OUT/AnimatedGIF.o"
# Real, hardware-free firmware files — compiled as-is (Dino/Flappy game logic,
# Godzilla speedometer GIF/PSRAM rendering, PhotoFrameApp WiFi portal + photo
# carousel). NES/SMS and OTAManager's real network OTA remain stubbed in
# firmware_stubs.cpp — see its header comment for why.
for f in screen_game ui_godzillaspeedometer PhotoFrameApp; do
  g++ -std=c++17 -O1 \
    "${CXXINC[@]}" \
    -DLV_CONF_INCLUDE_SIMPLE -DEMU_SDCARD_ROOT="\"$SDCARD_ROOT\"" \
    -c "$SK/$f.cpp" -o "$OUT/$f.o"
done

echo "==> Compiling C sources (SquareLine UI + LVGL + icons)..."
# Jimnylogo.c / wifi_qr.c: SquareLine-exported lv_img_dsc_t image assets used
# by PhotoFrameApp.cpp's boot splash and WiFi-upload-overlay QR code — not
# matched by the ui*.c glob below, so listed explicitly alongside grid_icons.c.
for f in grid_icons Jimnylogo wifi_qr; do
  g++ -x c -std=c11 -O1 \
    "${INC[@]}" \
    -DLV_CONF_INCLUDE_SIMPLE \
    -c "$SK/$f.c" -o "$OUT/$f.o"
done
for f in "$SK"/ui*.c; do
  name=$(basename "$f" .c)
  # ui_gridlauncher.c is dead/vestigial SquareLine code: ui_uigridlauncher is
  # declared but never referenced anywhere in the real .ino's actual flow
  # (the real grid launcher is the hand-coded build_grid_launcher(), built on
  # ui_uilauncher). The real toolchain's --gc-sections silently strips it;
  # our native link requires full symbol resolution, so skip it explicitly.
  if [ "$name" = "ui_gridlauncher" ]; then continue; fi
  g++ -x c -std=c11 -O1 \
    "${INC[@]}" \
    -DLV_CONF_INCLUDE_SIMPLE \
    -c "$f" -o "$OUT/ui_src_$name.o"
done
for f in $LVGL_SRC; do
  name=$(echo "$f" | tr '/' '_')
  g++ -x c -std=c11 -O1 \
    "${INC[@]}" \
    -DLV_CONF_INCLUDE_SIMPLE \
    -c "$f" -o "$OUT/lvgl_$name.o"
done

echo "==> Linking..."
g++ -std=c++17 $(pkg-config --libs sdl2) \
  "$OUT"/*.o \
  -o "$OUT/trailmaster_emulator"

# Wrap the binary in a minimal .app bundle so it's double-clickable from
# Finder (no Terminal/EMU_FORCE_SCREEN needed for normal use) — same binary,
# just packaged the way macOS expects a GUI app to look.
APP="$OUT/Trailmaster Emulator.app"
mkdir -p "$APP/Contents/MacOS"
cp "$OUT/trailmaster_emulator" "$APP/Contents/MacOS/trailmaster_emulator"
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key><string>trailmaster_emulator</string>
    <key>CFBundleIdentifier</key><string>com.trailmaster.emulator</string>
    <key>CFBundleName</key><string>Trailmaster Emulator</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST

echo "==> Built $OUT/trailmaster_emulator"
echo "==> Double-clickable app: $APP"

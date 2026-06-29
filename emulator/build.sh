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
mkdir -p "$OUT"

# Generate the build-time sketch copy (see header comment above).
sed '1790s/.*extern bool dino_ready;.*/                \/\/ (emulator build: redundant extern stripped, see emulator\/PLAN.md)/' \
  "$SK/$SKNAME.ino" > "$OUT/sketch_main.cpp"

LVGL_SRC=$(find "$LVGL/src" -name '*.c')

echo "==> Compiling C++ sources..."
g++ -std=c++17 -O1 $(pkg-config --cflags sdl2) \
  -I "$DIR/core" -I "$DIR/board" -I "$DIR/board/fake_esp_idf" -I "$SK" -I "$LVGL" -I "$LVGL/src" \
  -DLV_CONF_INCLUDE_SIMPLE -DEMU_SDCARD_ROOT="\"$SDCARD_ROOT\"" \
  -c "$OUT/sketch_main.cpp" -o "$OUT/sketch_main.o"
g++ -std=c++17 -O1 $(pkg-config --cflags sdl2) \
  -I "$DIR/core" -I "$DIR/board" -I "$DIR/board/fake_esp_idf" -I "$SK" -I "$LVGL" -I "$LVGL/src" \
  -DLV_CONF_INCLUDE_SIMPLE -DEMU_SDCARD_ROOT="\"$SDCARD_ROOT\"" \
  -c "$DIR/core/Arduino.cpp" -o "$OUT/Arduino.o"
g++ -std=c++17 -O1 $(pkg-config --cflags sdl2) \
  -I "$DIR/core" -I "$DIR/board" -I "$DIR/board/fake_esp_idf" -I "$SK" -I "$LVGL" -I "$LVGL/src" \
  -DLV_CONF_INCLUDE_SIMPLE \
  -c "$DIR/runtime/main.cpp" -o "$OUT/main.o"
for f in amoled_sim ft3168_sim qmi8658c_sim firmware_stubs; do
  g++ -std=c++17 -O1 \
    -I "$DIR/core" -I "$DIR/board" -I "$DIR/board/fake_esp_idf" -I "$SK" -I "$LVGL" -I "$LVGL/src" \
    -DLV_CONF_INCLUDE_SIMPLE -DEMU_SDCARD_ROOT="\"$SDCARD_ROOT\"" \
    -c "$DIR/board/$f.cpp" -o "$OUT/$f.o"
done
# Real, hardware-free firmware file (Dino/Flappy game logic) — compiled as-is.
g++ -std=c++17 -O1 \
  -I "$DIR/core" -I "$DIR/board" -I "$DIR/board/fake_esp_idf" -I "$SK" -I "$LVGL" -I "$LVGL/src" \
  -DLV_CONF_INCLUDE_SIMPLE -DEMU_SDCARD_ROOT="\"$SDCARD_ROOT\"" \
  -c "$SK/screen_game.cpp" -o "$OUT/screen_game.o"

echo "==> Compiling C sources (SquareLine UI + LVGL + icons)..."
g++ -x c -std=c11 -O1 \
  -I "$DIR/core" -I "$DIR/board" -I "$DIR/board/fake_esp_idf" -I "$SK" -I "$LVGL" -I "$LVGL/src" \
  -DLV_CONF_INCLUDE_SIMPLE \
  -c "$SK/grid_icons.c" -o "$OUT/grid_icons.o"
for f in "$SK"/ui*.c; do
  name=$(basename "$f" .c)
  # ui_gridlauncher.c is dead/vestigial SquareLine code: ui_uigridlauncher is
  # declared but never referenced anywhere in the real .ino's actual flow
  # (the real grid launcher is the hand-coded build_grid_launcher(), built on
  # ui_uilauncher). The real toolchain's --gc-sections silently strips it;
  # our native link requires full symbol resolution, so skip it explicitly.
  if [ "$name" = "ui_gridlauncher" ]; then continue; fi
  g++ -x c -std=c11 -O1 \
    -I "$DIR/core" -I "$DIR/board" -I "$DIR/board/fake_esp_idf" -I "$SK" -I "$LVGL" -I "$LVGL/src" \
    -DLV_CONF_INCLUDE_SIMPLE \
    -c "$f" -o "$OUT/ui_src_$name.o"
done
for f in $LVGL_SRC; do
  name=$(echo "$f" | tr '/' '_')
  g++ -x c -std=c11 -O1 \
    -I "$DIR/core" -I "$DIR/board" -I "$DIR/board/fake_esp_idf" -I "$SK" -I "$LVGL" -I "$LVGL/src" \
    -DLV_CONF_INCLUDE_SIMPLE \
    -c "$f" -o "$OUT/lvgl_$name.o"
done

echo "==> Linking..."
g++ -std=c++17 $(pkg-config --libs sdl2) \
  "$OUT"/*.o \
  -o "$OUT/trailmaster_emulator"

echo "==> Built $OUT/trailmaster_emulator"

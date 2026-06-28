#!/usr/bin/env bash
# Build the Trailmaster LVGL simulator to WebAssembly.
# Output: sim/build/trailmaster_sim.{html,js,wasm}
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKETCH="$DIR/../waveshare_esp32s3_1.43_amoled_lvgl8"
LVGL="$SKETCH/libraries/lvgl"
OUT="$DIR/build"
mkdir -p "$OUT"

# LVGL core/extra sources (the sim's lv_conf.h disables ESP-only bits like GIF).
LVGL_SRC=$(find "$LVGL/src" -name '*.c')
# SquareLine UI (hardware-free): screens, components, helpers, fonts, images.
UI_SRC=$(ls "$SKETCH"/ui*.c)

emcc \
  -I"$DIR" -I"$SKETCH" -I"$LVGL" -I"$LVGL/src" \
  -DLV_CONF_INCLUDE_SIMPLE \
  "$DIR/main.c" "$DIR/sim_screens.cpp" "$DIR/sim_ota_stub.cpp" "$DIR/sim_stubs.c" $UI_SRC $LVGL_SRC \
  -s USE_SDL=2 \
  -s WASM=1 \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s TOTAL_MEMORY=67108864 \
  -s EXPORTED_RUNTIME_METHODS=ccall \
  --shell-file "$DIR/shell.html" \
  -O2 \
  -o "$OUT/trailmaster_sim.html"

echo "==> Built $OUT/trailmaster_sim.html"

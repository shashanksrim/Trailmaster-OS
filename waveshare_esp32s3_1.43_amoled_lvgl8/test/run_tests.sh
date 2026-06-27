#!/usr/bin/env bash
# Build and run the host-side unit tests for Trailmaster-OS pure logic.
# No ESP32 / Arduino toolchain needed — just a C++ compiler.
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKETCH_DIR="$(dirname "$DIR")"   # headers live one level up

OUT="$DIR/test_ota"
echo "==> Compiling tests..."
g++ -std=c++17 -Wall -Wextra -I"$SKETCH_DIR" "$DIR/test_ota.cpp" -o "$OUT"

echo "==> Running tests..."
"$OUT"

#!/usr/bin/env bash
#
# Trailmaster-OS — one-command OTA publisher
#
# Repo: https://github.com/shashanksrim/Trailmaster-OS  (this parent folder is
# the repo root, so firmware.bin / version.json live right here).
#
# Usage:
#   ./publish.sh <path-to-exported.bin> ["changelog text"]            # prep + push
#   ./publish.sh --prepare <path-to-exported.bin> ["changelog text"]  # prep only,
#       then commit + push in GitHub Desktop (review the diff first)
#
# Release flow (low-effort):
#   1. Edit the version in  waveshare_esp32s3_1.43_amoled_lvgl8/version.h
#         #define APP_VERSION "3.2"
#   2. Arduino IDE: Sketch -> Export Compiled Binary, grab the .ino.bin
#      (or: arduino-cli compile --profile amoled --output-dir build_out
#           inside the sketch folder, then use build_out/*.ino.bin)
#   3. ./publish.sh <the .ino.bin> "Fixed speedo flicker"
#
# The script reads the version FROM version.h (so version.json can never disagree
# with the firmware you compiled), copies the binary to firmware.bin, updates
# version.json, then commits & pushes to GitHub.
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERSION_H="$REPO_ROOT/waveshare_esp32s3_1.43_amoled_lvgl8/version.h"
VERSION_JSON="$REPO_ROOT/version.json"
MANIFEST="$REPO_ROOT/docs/manifest.json"
FW_DEST="$REPO_ROOT/firmware.bin"
FW_URL="https://raw.githubusercontent.com/shashanksrim/Trailmaster-OS/main/firmware.bin"

# --prepare : do the file prep but DON'T commit/push — leaves that to GitHub Desktop.
PREPARE_ONLY=0
if [ "${1:-}" = "--prepare" ] || [ "${1:-}" = "-p" ]; then
  PREPARE_ONLY=1; shift
fi

if [ $# -lt 1 ]; then
  echo "Usage: ./publish.sh [--prepare] <path-to-exported.bin> [\"changelog\"]" >&2
  exit 1
fi
BIN_SRC="$1"
CHANGELOG="${2:-Trailmaster-OS firmware update}"

if [ ! -f "$BIN_SRC" ]; then
  echo "ERROR: firmware file not found: $BIN_SRC" >&2
  exit 1
fi

VERSION="$(sed -n 's/.*#define APP_VERSION "\([^"]*\)".*/\1/p' "$VERSION_H")"
if [ -z "$VERSION" ]; then
  echo "ERROR: could not read APP_VERSION from $VERSION_H" >&2
  exit 1
fi

echo "==> Publishing Trailmaster-OS v$VERSION"
echo "    binary : $BIN_SRC"
echo "    notes  : $CHANGELOG"

cp "$BIN_SRC" "$FW_DEST"
SIZE_KB=$(( $(wc -c < "$FW_DEST") / 1024 ))
echo "==> firmware.bin updated (${SIZE_KB} KB)"

cat > "$VERSION_JSON" <<EOF
{
  "version": "$VERSION",
  "changelog": "$CHANGELOG",
  "firmware_url": "$FW_URL",
  "sd_files": []
}
EOF
echo "==> version.json updated"

# Keep the web-flasher manifest version in sync (cosmetic but tidy)
if [ -f "$MANIFEST" ]; then
  sed -i '' "s/\"version\": *\"[^\"]*\"/\"version\": \"$VERSION\"/" "$MANIFEST"
  echo "==> docs/manifest.json version synced"
fi

if [ "$PREPARE_ONLY" = "1" ]; then
  echo "==> Files prepared for v$VERSION."
  echo "    Open GitHub Desktop -> review the changed files -> commit -> Push origin."
  echo "    (firmware.bin, version.json, docs/manifest.json, version.h)"
  exit 0
fi

cd "$REPO_ROOT"
git add firmware.bin version.json waveshare_esp32s3_1.43_amoled_lvgl8/version.h docs/manifest.json
git commit -m "Release v$VERSION: $CHANGELOG"
git push origin main

echo "==> Done. Device: About -> Check for Update -> Install v$VERSION"

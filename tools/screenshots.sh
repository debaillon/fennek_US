#!/usr/bin/env bash
# =============================================================================
# screenshots.sh — automatically generate README screenshots without the device.
#
# Render five English Fennek screens with the host renderer
# (tools/screenshot.cpp, real Adafruit-GFX drawing code for pixel accuracy)
# and export them as PNGs under docs/screenshots/en/.
#
#   tools/screenshots.sh [scale]   (default: 2)
#
# Requirements: g++, python3 + Pillow, and one run of `pio run -e fennek`
# (to place the Adafruit-GFX library under .pio/libdeps/).
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")/.."

OUT="docs/screenshots"
SCALE="${1:-2}"

GFX="$(find .pio/libdeps -maxdepth 2 -type d -name 'Adafruit GFX Library' 2>/dev/null | head -1)"
if [ -z "$GFX" ]; then
  echo "Adafruit-GFX not found — first run 'pio run -e fennek'." >&2
  exit 1
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "==> compiling host renderer ..."
g++ -std=c++17 -O2 -w -DARDUINO=10805 \
  -Itools/hostshim -Isrc -I"$GFX" \
  tools/screenshot.cpp src/core/gui.cpp "$GFX/Adafruit_GFX.cpp" \
  -o "$TMP/fennek_shot"

"$TMP/fennek_shot" "$TMP"
echo "==> exporting English PNGs to $OUT/en (scale ${SCALE}x) ..."
python3 tools/make_screenshots.py "$TMP" "$OUT/en" "$SCALE"

echo "Done."

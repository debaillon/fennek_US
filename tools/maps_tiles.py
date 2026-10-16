#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Dr. Daniel Dumke
# =============================================================================
# maps_tiles.py — download OSM tiles, dither them to 1 bit, and package them
# for the Fennek Maps app. Runs on a PC (NOT on the device); copy the output to SD.
#
# Ausgabeformat (passend zu services/maps_tiles + Adafruit_GFX::drawBitmap):
#   /maps/{z}/{x}/{y}.bin  — 256x256 px, 1-bit, row-major, MSB zuerst,
#   32 bytes/row = 8192 bytes. Set bit = black (land/lines).
#
# Example (Dortmund, zoom 13–16), writing to the local ./sdcard/maps directory:
#   python tools/maps_tiles.py --bbox 51.40 7.30 51.56 7.62 \
#       --zoom 13-16 --out ./sdcard
# Then copy the contents of ./sdcard to the SD card (output is ./sdcard/maps).
#
# Select a tile source with --preset (or use --url for a custom source):
#   --preset standard          OSM standard layer (default, directly from OSM)
#   --preset cyclosm           CyclOSM — emphasizes cycling and path networks
#   --preset topo              OpenTopoMap — hiking trails, paths, and contours
#   --preset cassius           OSM standard via local tile proxy (cassius:8765)
#   --preset cassius-cyclosm   CyclOSM via proxy
#   --preset cassius-topo      Topo via proxy
# Higher detail levels (z14–16) are most useful with cyclosm/topo.
# With cassius presets, the first run fetches from OSM and caches on cassius;
# subsequent runs use the proxy cache immediately (--delay 0 is then possible).
#
# Notes:
#  * Each source has its own tile usage policy; do not perform bulk downloads.
#    For larger areas, use your own renderer/MBTiles server (set --url).
#    A User-Agent is provided.
#  * Adjust the threshold/inversion for each layer with --threshold and --invert;
#    the default threshold is tuned for each preset.
#  * PNG caches are kept separate by source under <cache>/<preset>.
#
# Dependency: Pillow (pip install pillow)
# =============================================================================
import argparse
import hashlib
import math
import os
import sys
import time
import urllib.request

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is missing — install it with 'pip install pillow'.")

TILE_PX = 256
USER_AGENT = "fennek-maps-tiles/1.0 (https://github.com/danst0/fennek)"

# Preset tile sources. Each has its own usage policy (no bulk downloads);
# use your own renderer for large areas. The default threshold is tuned for
# each layer (topo/cyclosm are more colorful and lighter than the standard
# style) and can be overridden with --threshold.
#
# cassius-* — local tile proxy (http://cassius:8765) with persistent cache;
# avoids OSM rate limits, and --delay 0 is possible after the initial warm-up.
# Setup: /home/danst/dockers/tile-proxy/ on cassius.
CASSIUS = "http://10.0.2.71:8765"
PRESETS = {
    # OSM standard layer (Carto): bold roads, lighter paths.
    "standard":         {"url": "https://tile.openstreetmap.org/{z}/{x}/{y}.png",
                         "threshold": 160},
    # CyclOSM: emphasizes cycling and path networks.
    "cyclosm":          {"url": "https://a.tile-cyclosm.openstreetmap.fr/cyclosm/{z}/{x}/{y}.png",
                         "threshold": 190},
    # OpenTopoMap: hiking trails, paths, and topographic contours.
    "topo":             {"url": "https://a.tile.opentopomap.org/{z}/{x}/{y}.png",
                         "threshold": 180},
    # Local Cassius proxy — same layers, no direct OSM connection.
    "cassius":          {"url": f"{CASSIUS}/osm/{{z}}/{{x}}/{{y}}.png",
                         "threshold": 160},
    "cassius-cyclosm":  {"url": f"{CASSIUS}/cyclosm/{{z}}/{{x}}/{{y}}.png",
                         "threshold": 190},
    "cassius-topo":     {"url": f"{CASSIUS}/topo/{{z}}/{{x}}/{{y}}.png",
                         "threshold": 180},
}
DEFAULT_URL = PRESETS["standard"]["url"]


def deg2tile(lat, lon, z):
    """Convert WGS84 coordinates to a Slippy Map tile index (x, y) at zoom z."""
    n = 1 << z
    x = int((lon + 180.0) / 360.0 * n)
    lat_rad = math.radians(lat)
    y = int((1.0 - math.asinh(math.tan(lat_rad)) / math.pi) / 2.0 * n)
    return max(0, min(n - 1, x)), max(0, min(n - 1, y))


def pack_1bit(img, threshold, invert):
    """Convert a dithered 256x256 grayscale image to 8192 bytes, MSB first; set bit = black."""
    img = img.convert("L").resize((TILE_PX, TILE_PX))
    # Floyd–Steinberg dithering to 1 bit; in PIL mode '1', 1 = white and 0 = black.
    bw = img.point(lambda p: 255 if p >= threshold else 0).convert("1")
    px = bw.load()
    out = bytearray(TILE_PX * TILE_PX // 8)
    for y in range(TILE_PX):
        for x in range(TILE_PX):
            black = (px[x, y] == 0)
            if invert:
                black = not black
            if black:
                out[(y * TILE_PX + x) >> 3] |= 0x80 >> (x & 7)
    return bytes(out)


def fetch_tile(url_tmpl, z, x, y, cache_dir, retries=3):
    """Load a PNG tile (using the local cache when available) and return a PIL image."""
    if cache_dir:
        cp = os.path.join(cache_dir, str(z), str(x), f"{y}.png")
        if os.path.exists(cp):
            return Image.open(cp)
    url = url_tmpl.format(z=z, x=x, y=y)
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    for attempt in range(retries):
        try:
            with urllib.request.urlopen(req, timeout=30) as r:
                data = r.read()
            break
        except Exception:
            if attempt == retries - 1:
                raise
            time.sleep(2 ** attempt)   # 1 s, 2 s
    if cache_dir:
        cp = os.path.join(cache_dir, str(z), str(x), f"{y}.png")
        os.makedirs(os.path.dirname(cp), exist_ok=True)
        with open(cp, "wb") as f:
            f.write(data)
    from io import BytesIO
    return Image.open(BytesIO(data))


def parse_zoom(s):
    if "-" in s:
        a, b = s.split("-", 1)
        return range(int(a), int(b) + 1)
    return [int(s)]


def main():
    ap = argparse.ArgumentParser(description="OSM tiles -> Fennek 1-bit tiles")
    ap.add_argument("--bbox", nargs=4, type=float, required=True,
                    metavar=("LAT1", "LON1", "LAT2", "LON2"),
                    help="Bounding box (two corners, either order)")
    ap.add_argument("--zoom", required=True, help="Zoom level z or range z1-z2")
    ap.add_argument("--out", default="./sdcard", help="Output root (creates <out>/maps)")
    ap.add_argument("--preset", choices=sorted(PRESETS), default="standard",
                    help="Tile source: standard | cyclosm (bike paths) | topo (hiking trails)")
    ap.add_argument("--url", default=None,
                    help="Tile URL template {z}/{x}/{y} (overrides --preset)")
    ap.add_argument("--cache", default="./tile_cache", help="PNG cache directory ('' = disabled)")
    ap.add_argument("--threshold", type=int, default=None,
                    help="Black/white threshold, 0–255 (default depends on --preset)")
    ap.add_argument("--invert", action="store_true", help="Invert black and white")
    ap.add_argument("--delay", type=float, default=0.2, help="Delay between downloads (seconds)")
    args = ap.parse_args()

    # Resolve the preset; explicit --url and --threshold take precedence.
    preset = PRESETS[args.preset]
    url_tmpl = args.url if args.url is not None else preset["url"]
    threshold = args.threshold if args.threshold is not None else preset["threshold"]
    print(f"Source: {args.preset} ({url_tmpl})  threshold={threshold}")

    lat1, lon1, lat2, lon2 = args.bbox
    lat_min, lat_max = min(lat1, lat2), max(lat1, lat2)
    lon_min, lon_max = min(lon1, lon2), max(lon1, lon2)
    # Keep the PNG cache separate by source so z/x/y.png from one source is not
    # reused for another (for example, after switching from standard to topo).
    cache = None
    if args.cache:
        key = (args.preset if args.url is None
               else "custom-" + hashlib.md5(url_tmpl.encode()).hexdigest()[:8])
        cache = os.path.join(args.cache, key)
    maps_root = os.path.join(args.out, "maps")

    total = made = skipped = 0
    for z in parse_zoom(args.zoom):
        x0, y0 = deg2tile(lat_max, lon_min, z)   # NW corner -> min x, min y
        x1, y1 = deg2tile(lat_min, lon_max, z)   # SE corner -> max x, max y
        xs = range(min(x0, x1), max(x0, x1) + 1)
        ys = range(min(y0, y1), max(y0, y1) + 1)
        n = len(xs) * len(ys)
        total += n
        print(f"[z{z}] {len(xs)}x{len(ys)} = {n} tiles")
        for x in xs:
            out_dir = os.path.join(maps_root, str(z), str(x))
            os.makedirs(out_dir, exist_ok=True)
            for y in ys:
                out_path = os.path.join(out_dir, f"{y}.bin")
                if os.path.exists(out_path):
                    skipped += 1
                    continue
                try:
                    img = fetch_tile(url_tmpl, z, x, y, cache)
                    with open(out_path, "wb") as f:
                        f.write(pack_1bit(img, threshold, args.invert))
                    made += 1
                    if cache is None or not os.path.exists(
                            os.path.join(cache or "", str(z), str(x), f"{y}.png")):
                        time.sleep(args.delay)   # throttle only actual downloads
                except Exception as ex:   # noqa: BLE001
                    print(f"  ! z{z}/{x}/{y}: {ex}", file=sys.stderr)
    print(f"Done: {made} created, {skipped} already present, {total} planned -> {maps_root}")


if __name__ == "__main__":
    main()

# tools/

Host-side tools outside the firmware build; these are **not** flashed to the device.

## Screenshots (without the device)

E-Ink has no operating-system screenshot facility. Instead of dumping the
device framebuffer, `screenshot.cpp` renders the screens on the host using the
same Adafruit-GFX drawing code and CP437 fonts as the firmware, into a
`GFXcanvas1` (240×320, 1 bit). The result is **pixel-accurate** to the display,
using sample data.

Text comes from the same English string catalog as the firmware
(`core/i18n.h`). `screenshots.sh` writes the English screenshots to
`docs/screenshots/en/`. Existing localized screenshot folders are historical
assets and are not regenerated.

```bash
pio run -e fennek        # once, so Adafruit-GFX is available under .pio/libdeps/
bash tools/screenshots.sh            # -> docs/screenshots/en/*.png (2x)
bash tools/screenshots.sh 3          # use a different scale
```

Components:

- `screenshot.cpp` — renders the launcher, music player, Tic-Tac-Toe, settings,
  and standby screens. The drawing bodies mirror the source code because the
  real `draw()` functions are in anonymous namespaces or depend on device
  state: launcher `apps/launcher.cpp`, TTT `apps/ttt.cpp`, player
  `apps/music_app.cpp`, settings `apps/settings_app.cpp`, and standby
  `core/power.cpp`. Shared helpers (`core/gui.cpp` and the bitmap
  `core/sleep_img.h`) and Adafruit-GFX are linked directly.
- `hostshim/` — minimal Arduino/Print/pgmspace shims so Adafruit-GFX can be
  compiled on a PC (`GxEPD2_BW.h` provides only the color constants).
- `make_screenshots.py` — scales the PGMs and exports PNGs (requires Pillow).
- `screenshots.sh` — orchestrates compilation, rendering, and PNG export.

> When an app’s layout or text changes, update the mirrored drawing bodies in
> `screenshot.cpp` and regenerate the screenshots.

## Host tests

- `host_test_games.cpp` — unit tests for the Arduino-free game cores
  (2048/Minesweeper/Chess/Tic-Tac-Toe). See the file header for how to run them.

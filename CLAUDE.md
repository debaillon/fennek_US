# CLAUDE.md

## Project

**Fennek** is multi-app handheld firmware for the **LilyGO T-Deck Pro V1.1**
(ESP32-S3, 8 MB PSRAM), built with PlatformIO/Arduino. Remote:
`danst0/fennek`. Version: `FENNEK_VERSION` in `src/config.h`; log prefix
`[FENNEK]`; NVS namespace `fennek`; SD cache `/.fennek`. Apps: Music,
Audiobooks, Reading, Mesh, Games, Files, Notes, Alarms, Maps, Mental Math,
Flashcards, Calendar, and Todo. Firmware UI and serial output are localized;
rendered text goes through `gui::print` (UTF-8 → CP437).

- Active code is in `src/`; the only build environment is `fennek`.
  `lib/meshcore/` and `lib/ed25519/` contain the vendored MeshCore subset.
  The old Meck firmware is in Git history.
- `boards/t-deck_pro.json`; partition `default_16MB.csv` (6.5 MB app slots,
  3.4 MB SPIFFS, 20 KB NVS).

## Build and flash

```bash
pio run -e fennek              # build
pio run -e fennek -t upload    # flash (/dev/ttyACM0, 921600 baud)
pio device monitor -b 115200  # boot log + exception decoder
```

Host tests cover the Arduino-free cores; hardware behavior still requires
on-device verification. The firmware boots fully **without an SD card**.

Debug flags (`-D`): `AUDIO_DEBUG_GAP`, `PLAYLIST_SELFTEST`, `MESH_SMOKE_TEST`,
`GAMES_SMOKE_TEST`, `SLEEP_WAKE_TEST`, `BATTLOG`.

## Toolchain — do not upgrade

`espressif32@6.11.0` (Arduino-ESP32 2.0.17 / IDF 4.4),
`ESP32-audioI2S#2.0.6`, RadioLib ^7.3 (`RADIOLIB_GODMODE=1`), rweather/Crypto,
and densaugeo/base64. Upgrade only when explicitly requested.

## Architecture — audio anti-stutter invariants

**E-Ink, SD, and the LoRa SX1262 share the same SPI bus** (HSPI, SCLK 36/MOSI
33/MISO 47).

```
Core 0:  Audio decode task (priority 4) ── audio.loop()
Core 1:  Arduino loop() ── appmgr ── g_spiMutex (core/board.h)
Core 1:  Mesh pump (mesh_app::background → spiLock)
```

1. **Every SPI access (SD, E-Ink, LoRa) must be inside `spiLock()` /
   `spiUnlock()`.**
2. **Call `audio.*` only from the audio task** — the UI sends commands through
   a queue (`services/audio.cpp`).
3. **Release the SPI mutex while E-Ink is BUSY** (`setBusyCallback` in
   `core/display.cpp`).
4. **Use 256 KB PSRAM read-ahead** (`audio.setBufsize(-1, 256*1024)`).
5. **SPI runs at 4 MHz** (`SPI_BUS_HZ`) — 8 MHz corrupts SD writes.
6. **LoRa CS (GPIO 3) must be HIGH when idle**; the radio uses the existing
   `g_spi` instance (do NOT define `P_LORA_SCLK`).
7. **Refresh E-Ink only when content changes** (dirty flag); draw progress and
   status only with `renderRegion`.
8. **Use NVS for settings/bookmarks** (never the SPI bus). Use SD for bulk
   caches and mesh data. Mesh identity is stored in SPIFFS.
9. **When Wi-Fi is active, call `audio::stop()` and
   `mesh_client::setSuspended(true)`** (the webfm rule applies to every
   Wi-Fi service).

## Modules (`src/`)

- `config.h` — pins and constants.
- `core/board.*` — SPI bus, SD mount, `g_spiMutex`, `loraPower`, `gpsPower`.
  GPS (GPIO39) and DRV2605 (GPIO2) are off at boot. No 4G modem (this variant
  is audio with a PCM5102A DAC).
- `core/display.*` — GxEPD2 E-Ink, `render(fn,full)` /
  `renderRegion(fn,y,h)`, BUSY callback, and `hibernate()` (default is
  `powerOff()` only; see pitfall 9).
- `core/gui.*` — `toCp437`/`print`/`printAt`/`textBounds`, `drawButton`,
  `drawRowText`, `Rect`.
- `core/touch.*` and `core/hyn/` — CST328 (I2C 13/14). **Do not refactor
  `hyn/`.**
- `core/keyboard.*` — TCA8418 (I2C 0x34), sticky Shift/Alt/Sym, key repeat
  400/150 ms.
- `core/battery.*` — BQ27220 fuel gauge (read-only).
- `core/settings.*` — NVS: volume, last app, resume positions, bookmarks.
  **Wi-Fi profiles** (up to 20) are stored in compact blob `wifis` (migrated
  from old keys `wssid`/`wpass` into slot 0); APIs
  `wifiCount/wifiSsidAt/wifiPassAt/wifiSet/wifiRemove`; slot 0 provides legacy
  single-profile access (`wifiSsid`/`setWifiSsid`, INI export/webfm).
- `core/console.*` — USB debug console (`help`). Commands: `status`,
  `time`/`tz`, `eink hib`, `mesh init`, `mesh eco`, `advert [flood]`,
  `public`/`join`/`chan`/`dm`, `pos`, `gps [scan|off]`, `wifi`, `alarm`,
  `ollama`, `podcast`, `ota`, `todo`, `cal`, `calibre`, `nav`, `scrobble`,
  `notes`, `rm`. Console input resets the auto-standby timer
  (`power::noteActivity()`).
- `core/power.*` — button (short press = key lock, long press = standby),
  auto-standby, deep sleep. **CPU clock governor:** base clock 80 MHz from
  `boostBegin()` (end of setup); `boostLock()`/`boostUnlock()` (reference
  counter) switches to 240 MHz for audio decoding, Wi-Fi (centrally through
  Wi-Fi events; services need not handle it), chess AI, EPUB conversion, and
  console commands. APB stays at 80 MHz; SPI/UART/I2S are unaffected.
  **Two side buttons: TOP=GPIO0 (firmware), BOTTOM=RST (hardware reset;
  immediate, invisible to firmware).** Timer wake uses a minimal path (banner
  only), with a full-screen refresh every 12 wakes. Alarm wake makes
  `handleTimerWake()` return `false` for a full boot. Safeguards added after
  three silent deep discharges (06-07/26): task watchdog around the minimal
  path and `enterStandby()`, release-wait timeout, `noteBoot()` crash-loop
  brake (3 abnormal resets → emergency standby with button-only wake),
  deep-discharge protection (≤5% → standby without hourly wake), and sleep
  diagnostics in RTC RAM (`logSleepDebug()` writes phase and wake history to
  battlog).
- `core/appmgr.*` — app lifecycle, status line, dirty-state coalescing,
  30-second persistence. Tapping the status line goes Home.
- `services/timesync.*` — no hardware RTC; canonical time is ESP32 system time.
  Sources in priority order: GPS > NTP > mesh adverts. Learns clock drift,
  syncs before standby, and uses POSIX-TZ (`tz` in NVS; default
  `Europe/Berlin`).
- `services/audio.*` — path-based queue (PSRAM staging), owner tokens
  (Music/Book/Podcast), shuffle/repeat, `seekRel`, sleep timer.
- `services/scrobble.*` — Navidrome/Subsonic. Wi-Fi and audio are mutually
  exclusive; batch scrobbles before auto-standby. Queue:
  `/.fennek/scrobbles.tsv`. NVS `nscro/nurl/nuser/npass`.
- `services/notes_ai.*` — Ollama cleanup of past notes. Batch before
  auto-standby, up to 20 per run. Done list `/.fennek/notes_ai.done` (SHA256).
  NVS `oai/ourl/omod`.
- `services/ota.*` — OTA through the GitHub Releases API (`danst0/fennek`).
  Resolves HTTP 302 redirects itself and flashes the inactive OTA slot. NVS
  `otau`. Release command:
  `gh release create vX.Y.Z .pio/build/fennek/firmware.bin`.
- `services/podcast.*` — **DISABLED (v2.5.9)**. Code remains available for
  reactivation. Subscriptions are in `/podcasts/feeds.txt`; only the newest
  episode per feed is retained.
- `services/library.*` — music under `/music`, growing PSRAM blocks (no track
  limit), cache `/.fennek/tracks.bin`. `TRACK_PATH_LEN`=256 (longer paths are
  skipped).
- `services/id3.*` — minimal ID3v1/v2 reader (TIT2/TPE1/TALB).
- `services/gps.*` — u-blox MIA-M10Q, UART1 38400 baud. Parses GGA/RMC. Sends
  UBX aiding (time + approximate position) at startup. GPS is active only
  while the Maps app is in the foreground.
- `services/maps_tiles.*` — 1-bit tiles `/maps/{z}/{x}/{y}.bin` (8192 B).
  PSRAM LRU cache (16 slots). Call `ensureViewport()` only from `tick()`.
- `services/textdoc.*` — streaming pagination and offset-index cache
  `/.fennek/idx/`.
- `services/epubzip.h` / `epubproc.*` — EPUB→TXT, cache
  `/books/.epub_cache/`.
- `services/alarmclock.*` — 4 alarms (NVS `alarm`), snooze in RTC RAM. Beep
  file `/.fennek/alarm.wav`, backlight blinking (GPIO42). Per-alarm signal
  mode (tone/blink/both). Auto-dismiss after 5 minutes.
- `services/calendar.*` and `apps/ical_core.h` — read-only iCal subscriptions.
  Feeds `/calendar/feeds.txt`. Conditional GET (ETag/304), streaming parser,
  window from 1 day ago through 56 days ahead. RRULE DAILY/WEEKLY/MONTHLY.
  NVS toggle `calauto`.
- `services/reinschrift.*` and `apps/reinschrift_core.h` — Todo sync over
  WebDAV (Nextcloud). Operation queue `/.fennek/todo_ops.tsv` and ETag-based
  PUT with conflict merge (412 → GET + merge). NVS
  `todourl/todousr/todopw/todopath`.
- `services/calibre_books.*` — e-book pull sync from **Calibre-Web** via OPDS
  (`calibre.dumke.me`; NOT the native Content Server). Shelf (default
  “Fennek”) → `/opds/shelfindex` + `/opds/download/<id>/epub/` → `/books`.
  Add-only; manifest `/.fennek/calibre.tsv`. Basic Auth uses the Calibre-Web
  login. Auto-sync at most every 6 hours before standby (lesson from podcast
  sync), max. 8 downloads per run. NVS `cburl/cbusr/cbpw/cbshelf/cbauto`.
  Console: `calibre`.
- `services/wifi.*` — centralized Wi-Fi. `connect(timeout)` applies the
  Wi-Fi rule (stop audio + suspend mesh), scans and chooses the **strongest
  known network in range** (up to 20 profiles in `settings::wifi*`; falls
  back to slot 0 for a hidden SSID). `disconnect()` is the counterpart;
  `pickBest()` is webfm’s separate state machine. Modem sleep is disabled for
  download throughput. All services (timesync/scrobble/ota/podcast/calendar/
  calibre/notes_ai/reinschrift/webfm) use this helper rather than their own
  `WiFi.begin` sequence.
- `services/webfm.*` — Wi-Fi file server (`http://fennek.local`), JSON API,
  and OTA endpoints. SD access under `spiLock`; network I/O after
  `spiUnlock`.
- `apps/launcher.*` — 2 pages with up to 10 tiles each. Page 1:
  Music/Audiobooks/Reading/Mesh/Options/Games/Files/Notes/Alarms/Maps. Page 2:
  Motion/Math/Learn/Calendar/Todo.
- `apps/music_app.*` — artist/album/playlist browser and player.
- `apps/book_app.*` — audiobooks under `/audiobooks`, NVS bookmarks, ±30 s.
- `apps/reader_app.*` — books under `/books` (.txt/.epub), progress display.
- `apps/mesh_client.*` — `MeshClient : BaseChatMesh`. SD persistence:
  `/meshcore/messages.log` (256 KB rotation), `/meshcore/contacts.bin`,
  `/meshcore/channels.txt`. SD writes are deferred (5-second throttle, never
  from a mesh callback).
- `apps/mesh_app.*` — channel/contact/DM screens. Radio initialization is
  lazy, on first entry.
- `apps/games_app.*` — 2048, Minesweeper, Chess (Negamax + alpha-beta, FreeRTOS
  task priority 1), TTT, Sudoku (iterative generator, no recursion). Logic is
  in Arduino-free `*_core.h` files and host-tested.
- `apps/files_app.*` — starts/stops WebFM and shows SSID/IP.
- `apps/notes_app.*` — daily notes `/notes/YYYY-MM-DD.md`.
- `apps/alarms_app.*` — alarm UI, direct numeric entry, ringing screen.
- `apps/maps_app.*` — maps and GPS marker. Load tiles in `tick()`, blit in
  `draw()`. Follow after >24 px movement. Follow mesh position after ≥30 s
  and >10 m.
- `apps/mathquiz_app.*` — mental math. CFO modes (SHARE/GROWTH/MARKUP/
  DISCOUNT/PCT/RULE72). Adaptive difficulty (per-mode skill counter persisted
  in NVS `mqlvl`). **Target time: ~20 seconds per problem:** operand ranges
  are capped and estimates have tolerance (`Problem.tol` /
  `toleranceFor` / `accept` in `mathquiz_core.h`). A good approximation is
  accepted for estimable problems (large products at medium level, money
  percentages, SHARE/GROWTH) with “Good estimate!” feedback and the exact
  answer shown; addition/subtraction/exact division/Rule72 remain exact.
  Feedback reports elapsed seconds.
- `apps/flashcards_app.*` — Leitner boxes 0–4, decks
  `/flashcards/*.txt`, progress `/flashcards/.progress/`.
- `tools/host_test_games.cpp` / `host_test_apps.cpp` — host tests for the
  Arduino-free cores.
- `main.cpp` — initialization order:
  `handleTimerWake()` → Power → Display → Touch/KB/Battery/Settings →
  SD/Library → Audio → Apps.

## Radio parameters

Default **“EU/UK Narrow”**: **869.618 MHz, BW 62.5 kHz, SF 8, CR 4/5**,
22 dBm, DIO2 RF switch, 2.4 V TCXO. NVS values
(`settings::meshParams()`) are authoritative; Settings app “Options → Radio”.
`initContacts()` **must** be called in `MeshClient::begin()` or the first
advert causes a NULL `contacts` crash.

**RX power-saving mode** (NVS `mesheco`, enabled by default; console
`mesh eco on|off`): SX1262 duty-cycle RX instead of continuous RX (saves about
2/3 of RX power). Implemented in the vendored wrapper
(`RadioLibWrappers.*`/`CustomSX1262Wrapper.h`): virtual `startRx()`, TIMEOUT
also on DIO1 (re-arm after a false preamble), and noise-floor sampling disabled
in duty mode (SPI probes wake the sleeping chip). Senders must use at least our
preamble length. Do not call raw `s_radio->startReceive()`; always call
`s_radioDrv->restartRecv()`.

## Known pitfalls (do not reintroduce)

1. **Call `initContacts()` from `MeshClient::begin()`** — otherwise the first
   advert causes a NULL crash.
2. **Never call `spiLock()` from code that already holds the lock** — the
   mutex is non-recursive and causes a complete freeze (loop, touch, keyboard,
   serial; no watchdog). `MeshClient::begin()`, all mesh callbacks, and
   `App::draw()` already run under `spiLock`.
3. **Do not blindly move the clock forward during sync.** Rules: forward jumps
   under 1 hour; move backward only after 3 adverts more than 1 hour behind.
   `s_clockConfident` bootstraps the first plausible jump after boot. ESP32
   system time survives deep sleep; `millis()` does not.
4. **Full contact table (`MAX_CONTACTS=64`):**
   `shouldOverwriteWhenFull() = true` replaces the oldest non-favorite.
5. **Auto-standby must account for console activity:**
   `power::noteActivity()` in `console::handleLine()`.
6. **Test standby on battery:** USB-JTAG-DTR can interpret GPIO0 press as a
   reset.
7. **Use flood, not zero-hop, for mesh position:** zero-hop does not reach
   one-hop bridges.
8. **GPS baud rate is 38400** (MIA-M10Q) — 9600 produces garbage.
9. **Keep E-Ink controller deep sleep (0x07) disabled by default.**
   `GxEPD2::hibernate()` sends the UC8253 to sleep with 0x07; only a hardware
   reset wakes it. On T-Deck Pro **V1.0, the EPD RST line is not wired**
   (LilyGO: `BOARD_EPD_RST -1`; GPIO 16 is the ALS interrupt. V1.1/V2 = 16,
   V3 = 9). The panel then hangs with BUSY active across firmware resets until
   it discharges itself after hours (Issue #2). Therefore
   `display::hibernate()` defaults to `powerOff()` only (the image remains
   without power); 0x07 is used only with NVS `einkhib` (console
   `eink hib on|off`), and a stuck panel (`display::panelStuck()`) permanently
   clears the flag. **Do not shorten the RST pulse:** GxEPD2 unconditionally
   sets `_hibernating=false`, so `initPanel()` tries 20/50/200 ms (previously
   2; GxEPD2 default is 10) and checks BUSY after each attempt.

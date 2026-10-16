# Open items

- [x] Calendar app (iCal, sparse sync) + Todo app (Reinschrift via
  Nextcloud/WebDAV)
  - **Done (v2.6.1):** Added two apps (tiles 13/14, page 2).
  - **Calendar (read-only):** Subscribes to `.ics` feeds
    (`/calendar/feeds.txt`) and displays an agenda. Sparse sync uses
    conditional GET (304), streaming parsing, and a forward window (56 days).
    Parser: `apps/ical_core.h` (RFC-5545 subset, RRULE DAILY/WEEKLY/MONTHLY,
    host-tested). Service: `services/calendar.*`; per-feed cache:
    `/calendar/<slug>.bin`. Console: `cal …`.
  - **Todo (Reinschrift, ../TodosExtension):** Syncs the Markdown database
    over Nextcloud/WebDAV (Basic Auth, GET/PUT). Conflict resolution is
    central: never blindly overwrite. Local changes use an operation queue
    addressed by `^` markers (`/.fennek/todo_ops.tsv`); sync is
    GET+ETag → applyOps merge → PUT with If-Match (on 412, merge once more).
    Edits to other tasks made elsewhere are preserved. Core:
    `apps/reinschrift_core.h` (host-tested merge/conflict scenarios). Service:
    `services/reinschrift.*`; app: `apps/todo_app` (complete, Quick Add, today/
    tomorrow, filters). Console: `todo …`.
  - Both apps use “Todo” / “Calendar” settings folders, Wi-Fi/audio
    exclusion, and sync via button plus before auto-standby (with back-off).
    Host tests passed (608k checks); build passed.
  - **Still needs device verification:** real WebDAV round-trip against
    globe.dumke.me and a real `.ics` feed; conflict path across two devices;
    E-Ink usability.
- [x] Battery display stuck at 72%
  - **Done (`battery.cpp`):** `percent()` no longer reads the gauge SoC
    register (`REG_SOC`); it derives charge from the LiPo discharge curve using
    voltage (`percentFromMilliVolts`, linear interpolation).
  - Diagnosis: the BQ27220 is `sealed`; its coulomb counter only resynchronizes
    FCC after a clean full charge. On the development device, reboots while
    flashing interrupt charging and FCC drifts low (1272 instead of 1400 mAh),
    leaving SoC stuck at 72% even though 4136 mV corresponds to about 90% on a
    real LiPo curve. The v2.4.8 RM/DC attempt and v2.4.9 revert both focused on
    the gauge register; the actual issue is that the register is unreliable.
    Terminal voltage tracks the real charge level instead (under the light,
    steady E-Ink load it barely falls below resting voltage). The `gauge`
    dump now shows both values side by side (unused gauge SoC vs. displayed
    value). Build passed.
  - **Still needs device verification:** run `gauge` at full charge and confirm
    the display is near 100%; verify that the percentage falls monotonically
    over a discharge cycle.
- [x] Can USB-C speed up SD-card transfers (OTG)?
  - **Done (analysis only; no code change, not worthwhile):** The bottleneck is
    not the USB/Wi-Fi transport but the shared HSPI bus used by the SD card:
    `SPI_BUS_HZ = 4 MHz` (`config.h:30`,
    `SD.begin(PIN_SD_CS, g_spi, SPI_BUS_HZ)` in `board.cpp:90`). 8 MHz
    corrupts writes (verified on hardware), so 4 MHz is the hard limit.
    Theoretical throughput is ~500 KB/s; actual throughput is ~250–400 KB/s.
    Every SD read, whether via WebFM/Wi-Fi or USB, uses that bus.
  - Why OTG/USB-MSC would not help:
    1. ESP32-S3 USB is Full-Speed only (USB 1.1, 12 Mbit/s, about 1 MB/s), with
       no High-Speed PHY. Even ideal USB-MSC tops out around 1 MB/s, while the
       4 MHz SD link (~0.5 MB/s) is slower. USB does not remove the actual
       bottleneck.
    2. USB-MSC requires exclusive block access: the host and the firmware’s
       mounted FAT filesystem cannot access the card simultaneously without
       risking corruption. All firmware SD use (audio/apps) would need to stop
       during transfers; MSC would still use the shared bus under `spiLock`.
       This is major architectural disruption for no speed gain.
    3. The real speedup would be SD_MMC in 4-bit mode (SDIO, 20–40 MHz), but it
       is not wired: SD shares the HSPI pins with E-Ink/LoRa and has no
       dedicated SDMMC data lines. This requires a hardware change.
  - Conclusion: USB-OTG/MSC offers no measurable benefit; the 4 MHz SPI SD bus
    remains the bottleneck. Do not implement.
- [x] What other optimizations are possible?
  - **Analysis:** Most areas are already tightly optimized: NVS uses
    write-on-change plus delta (`settings.cpp:142-154`, avoiding flash wear);
    SD scans are chunked and pause during playback; refreshes are region-scoped;
    audio runs on its own core with 256 KB PSRAM read-ahead. Most easy wins
    were already taken. Remaining options, by value:
    1. Idle CPU/power (main opportunity): `main loop()` had a fixed
       `delay(10)` (100 Hz polling), waking Core 1 every 10 ms even when the
       device was awake but idle (static E-Ink, no audio).
    2. `[APP] Tap` serial spam (`appmgr.cpp`): logged each tap; a held finger
       (known cosmetic repeated-tap issue) flooded serial.
    3. `esp_pm` automatic light sleep could save the most idle power, but is
       risky with I2S/SPI/audio tasks and requires device power measurements.
       **Not implemented** to preserve the verify-on-device discipline.
    4. `background()` fan-out (all apps each loop) is intentional and cheap
       (no-op virtual methods), so it was left alone.
  - **Done (implemented; build passed, 27.7%):**
    1. Adaptive input polling (`main.cpp`): `power::idleMs()` (new getter in
       `core/power`, milliseconds since last activity; active playback keeps
       it near zero). The loop stays near 100 Hz while busy (`idleMs < 3 s` or
       WebFM is serving HTTP), then drops to about 30 Hz (`delay(33)`). Touch
       and keyboard remain responsive (key repeat/debounce use `millis()`;
       audio is on Core 0). Applies only during awake idle before auto-standby.
    2. Tap logging is throttled to once per ≥400 ms (static timestamp); tap
       processing is unchanged, and serial stays clear while a finger is held.
  - **Still needs device verification:** confirm touch and typing still feel
    responsive during idle at 33 ms polling; optionally measure whether idle
    power measurably drops. Evaluate `esp_pm` light sleep separately if desired.
- [x] Disable the Podcast app because Wi-Fi sync is too slow.
  - **Done (v2.5.9, soft-disabled):** The pain point was automatic Wi-Fi sync
    before standby (`podcast::flushBeforeStandby()` in `core/power.cpp`),
    which brought up Wi-Fi at EVERY standby and downloaded an episode (often
    >100 MB) over the 4 MHz SD bus. This sync was commented out. The launcher
    tile (`main.cpp:229`), app registration (`main.cpp:215`), and
    `podcast::begin()` (`main.cpp:151`) were also commented out, removing the
    app from the device; slot 13 (page 2) remains empty (label None is skipped).
    **Intentionally retained for reactivation:** `services/podcast.*`,
    `apps/podcast_app.*`, `podcast_core.h` (still compiles), console commands
    `podcast …`, NVS toggle `pcas`, and i18n strings. Build passed (flash
    27.7%). Status is recorded in `CLAUDE.md`.

- [ ] Transcribe voice notes.
  - **Note:** Do this only after confirming on the device that WAV recordings
    contain audible sound (see the completed item below). Then use the same
    Wi-Fi/audio batch model as `notes_ai`/scrobble (external Whisper/service,
    run before auto-standby).
- [ ] Sync with Obsidian later.

# On-device verification — new features from 06/17 (v2.0.x)

- [ ] Verify on device (build passed): mesh identity loaded from SD, chat
  navigation, reader refresh without full-screen flashing, and opening the
  same book twice produces a cache hit.
- [ ] Verify on device (build passed, 06/12): battery percentage on standby
  screen + timer wake (flash with `-D SLEEP_WAKE_TEST` for 60 seconds instead
  of 1 hour; deep sleep disconnects USB, so reconnect the monitor) and Web file
  manager (console: `wifi ssid/pass/start`; browser: upload/download/delete;
  uploaded MP3 plays; `wifi stop` restores mesh reception).

# Completed

- [x] Add another game; suggest an option.
  - **Done:** Sudoku was selected (well suited to E-Ink: static 9×9 grid, no
    flicker; arithmetic logic is host-testable). Added as the fifth game in
    “Games.” `apps/sudoku.*` provides the UI; Arduino-free
    `apps/sudoku_core.h` provides generator and solver and is host-tested
    (`tools/host_test_games.cpp`: `testSudoku` checks unique solution, valid
    `sol[]`, and conflict detection). The generator uses randomized
    backtracking for a complete solution, then digs out cells while uniqueness
    holds (`countSolutions` stops at 2); the backtracker is iterative and
    non-recursive (depth 81 would overflow the loop-task stack). Easy/Medium/
    Hard difficulty (40/32/26 clues), touch tap/WASD cursor, number keys 1–9,
    0/Backspace/Microphone to clear, inverted conflict cells, and a timed
    “Solved” overlay. Best records in NVS (`settings::sudokuSolved` /
    `settings::sudokuBestSec`, added to INI export/import). `games_app` menu
    now has five entries (46-px buttons); added i18n `GameSudoku` and Sudoku
    to `GAMES_SMOKE_TEST`. Host tests passed (3068 checks); firmware builds.
  - **Still needs device verification:** generation time for Hard on ESP32,
    readability of 26-px cells on E-Ink, and number-entry ergonomics
    (Alt/Sym layer).
- [x] GPS time updates were not working.
  - **Done (`timesync.cpp`):** Two fixes. (1) `gpsSync()` now sets
    `s_lastGpsMs` (`gpsFresh`) only when the difference exceeds 2 seconds,
    i.e. for an actual satellite correction. Previously a zero difference
    (the GPS module echoed system time injected through UBX-MGA-INI) marked
    GPS fresh and blocked NTP/mesh for 10 minutes without providing a useful
    update. This fixes the “B1 INTENTIONALLY DISCARDED” issue.
    (2) `gpsSyncBeforeStandby()` no longer waits for `f.valid` (a position
    fix); RMC often provides UTC before a position fix (as Maps and console
    `gps` already did). An inline difference check (>2 s) prevents circular
    injection: if GPS echoes the injected system time (difference ≈0), no
    sync is counted and no back-off is consumed.
  - Diagnostic finding: indoors, the u-blox MIA-M10Q often sends RMC without
    a date, so `epochUtc = 0` and sync is impossible regardless of code. GPS
    time sync requires sky visibility; the Maps app and console `gps` support
    this.
  - **Still needs device verification:** with sky visibility, confirm `time`
    reports “Source=GPS Quality=good” after a short GPS session (Maps app or
    `gps 60`).
- [x] The web app reported “Update available: v2.4.7 → v2.3.1”.
  - **Done (v2.4.8):** Replaced `strcmp(r.latest, r.current) != 0` in
    `ota.cpp` with `semverGt(latest, current)` for semantic version comparison
    (numeric Major/Minor/Patch; accepts a `v` prefix). Verified on device:
    `ota update` with GitHub release v2.3.1 correctly reports “current”
    instead of falsely offering an update.
- [x] Critically test all recent changes.
  - **Done (v2.4.8/v2.4.9):** Host tests passed (mathquiz, flashcards,
    podcast, games); firmware builds. Verified on-device via console:
    `status` (heap/PSRAM/SD OK), `gauge` (BQ27220 responds), `podcast` (feed
    list correct), `alarm` (4 alarms), `ota update` (`semverGt` confirmed:
    v2.3.1 < v2.4.8, so no update). Discovered and fixed the incorrect RM/DC
    battery fix (see battery item). UI paths (mathquiz numeric entry, Notes
    microphone key, Podcast app, touch) remain to be manually verified on
    device; no test failures found.
- [x] Battery display only reached 72%.
  - **Done (diagnosis + revert, v2.4.9):** A clean charge cycle calibrated
    the BQ27220 gauge to FCC=1272 mAh (SOH 91% × 1400 mAh = 1274 mAh).
    At full charge (4.2 V), the SOC register shows 100%; 72% was correct for
    the current charge level (4136 mV). The v2.4.8 RM/DC fix was wrong
    (nominal DC=1400, learned FCC=1272 would cap a full battery at 91%) and
    v2.4.9 restored the SOC register. No firmware change was needed; gauge
    self-calibration resolved the issue.
- [x] Add a Podcast app with sync, retaining only the latest episode by
  default; start with Freakshow.fm.
  - **Done:** “Podcast” app (launcher tile “Podcast,” page 2, slot 13).
    `apps/podcast_app.*` and Arduino-free RSS parser `apps/podcast_core.h`
    (host-tested in `tools/host_test_apps.cpp`). Sync engine
    `services/podcast.*`: subscriptions in `/podcasts/feeds.txt` (default
    `https://freakshow.fm/feed/mp3/`, editable through WebFM); each feed uses
    `/podcasts/<slug>/episode.<ext>` and `state.txt`. “Keep only the latest”
    means load the first `<item>` during sync and delete older audio files.
    The service starts Wi-Fi itself (stop audio + suspend mesh), fetches only
    enough of the feed to complete the first `<item>`, then downloads in
    chunks to SD (resolves http/https redirects; network I/O after
    `spiUnlock`). Manual sync (app button/`podcast sync`) handles all feeds.
    Auto-sync before standby (`flushBeforeStandby` in `power::poll`, RTC-RAM
    back-off, NVS toggle `pcas`) handles one feed per standby (round-robin).
    Playback uses the audio queue (`Owner::Podcast`) with a resume bookmark
    like the book app. Console: `podcast` / `podcast feed/rm/on/off/sync`.
    Build and host tests passed.
  - **Still needs device verification:** real download (Freakshow episodes
    >100 MB; check SD space and duration), m4a/AAC playback, CDN redirect
    chains, and episode-title wrapping on E-Ink.
- [x] Document device constraints on the website and in the documentation
  (no clock, cannot stay online, two cores, E-Ink).
  - **Done:** Expanded `web/index.html` from “The defining quirk” to
    “Engineering around the hardware” (shared SPI bus plus four challenge
    tiles: no RTC, not always online, two cores, E-Ink, each with its
    solution); updated Podcast app tile and footer version to v2.4.6. Added
    “Engineering around the hardware” section and Podcast app row to the three
    README files.
- [x] The mental-math layout did not fit all problems.
  - **Done:** `quizMid()` in `mathquiz_app.cpp` now auto-fits. New `fitSize`
    helper chooses the largest font (3→2→1) whose measured pixel width fits
    within 228 px (problem + answer entry). Problem/answer/feedback are
    vertically centered as a block in the 108-px strip instead of using fixed
    offsets, preventing overflow/overlap for long CFO problems. Also fixed a
    latent centering bug: `textBounds` measured using header size 2 instead of
    `qsize`. Build passed.
  - **Still needs device verification:** inspect long CFO problems on device.
- [x] Expand flashcards substantially (500× Swedish), shuffle + review,
  10 cards per session.
  - **Done:** Added shuffle (Fisher–Yates), `stableSortByBox`, and
    `kSessionLimit=10` to host-tested `flashcards_core.h`. In
    `flashcards_app.cpp`, `buildQueue()` collects due cards, shuffles them,
    sorts by box ascending (less learned first; learned cards appear less
    often), then caps at 10 (`esp_random`, not `rand()`). Incorrect answers
    repeat in the same session. Content: `decks/schwedisch.txt` (703 cards;
    three older decks combined and expanded; no duplicates or invalid lines).
    README updated; build passed.
  - **Still needs device verification:** test a 10-card session and shuffling;
    upload the deck through WebFM.
- [x] Add an app for mental arithmetic practice.
  - **Done:** “Mental Math” app (launcher tile “Math,” slot 11).
    `apps/mathquiz_app.*` and Arduino-free core `apps/mathquiz_core.h`
    (host-tested in `tools/host_test_apps.cpp`). Modes: addition, subtraction,
    multiplication, division, and mixed, each at three levels. Subtraction is
    never negative; division is exact (digits-only input). Best streak in NVS
    (`settings::mathBestStreak`). Build passed.
  - **Still needs device verification:** numeric input and region refresh.
- [x] Add a flashcards app.
  - **Done:** “Flashcards” app (launcher tile “Learn,” slot 12).
    `apps/flashcards_app.*` and host-tested core `apps/flashcards_core.h`.
    Decks are text files in `/flashcards/*.txt` (`front<TAB>back`), using
    Leitner boxes 0–4. Per-deck progress stored in
    `/flashcards/.progress/<deck>.prg` (CRC-keyed). SD handling follows
    `notes_app`; build passed.
  - **Still needs device verification:** upload decks to `/flashcards` via
    WebFM, study, verify progress survives reboot, and check accented-text
    wrapping on E-Ink.
- [x] Add Swedish vocabulary, phrases, and conversation flashcards.
  - **Done (content):** Added `decks/schwedisch-vokabeln.txt` (37 cards),
    `decks/schwedisch-saetze.txt` (20), and
    `decks/schwedisch-konversation.txt` (15), plus `decks/README.md` with
    format and WebFM upload instructions for `/flashcards`. All cards parsed
    successfully in host check.
  - **Still needs device verification:** upload through WebFM.
- [x] Delete CT books.
  - Manual SD action on device, not a firmware feature; now possible through
    console command `rm /books/<calibre-path>`, including overlength paths.
- [x] Center maps on Düsseldorf when there is no GPS fix.
  - **Done (commit `70e20de9`):** `FALLBACK_LAT/LON = 51.2277/6.7735`
    (Düsseldorf) in `maps_app.cpp:31-32`.
  - Diagnosis: fallback center was Dortmund (51.4818/7.2162) in
    `maps_app.cpp:28-30` (`FALLBACK_LAT/LON`), so the map stayed there without
    a fix or mesh position. NRW tiles z6–13 cover Düsseldorf; Dortmund detail
    tiles z14–16 do not cover Düsseldorf.
  - Original plan: set fallback to Düsseldorf; check that `scanZooms()` finds
    an available level and `clampZoom()` clamps `DEFAULT_ZOOM` to an available
    level (otherwise the grid is empty), lowering `DEFAULT_ZOOM` to z13 if
    needed; verify on device that the no-fix map centers on Düsseldorf and
    renders tiles.
- [x] Show date and HH:MM for audio recordings.
  - **Done (commit `8370a7ba`):** `scanNotes()` derives the title from the WAV
    filename as `YYYY-MM-DD HH:MM` (`notes_app.cpp:196ff`).
  - Diagnosis: filename was already `YYYY-MM-DD-HHMMSS.wav`
    (`notes_app.cpp:319`), but every recording appeared only as “Voice note”
    in the list (`notes_app.cpp:196`), hiding date and time.
  - Plan: derive the WAV title in `scanNotes()` rather than always showing
    “Voice note.” Optionally shorten the filename to minutes, but keeping
    seconds prevents collisions for multiple recordings per minute; the
    recommendation is to keep seconds in the filename and show HH:MM only.
- [x] Headless alarm support: `time set`, `alarm 0 <hh:mm>`, list, scheduled
  ringing (minute match), `alarm test`/`stop`; all verified on device.
- [x] Alarm wake from deep sleep: device woke **by itself** from standby
  (`sleep`) at alarm time and rang (06/17, including while connected to USB).
  Core path confirmed: `enterStandby` → timer set to alarm time →
  `handleTimerWake` → full boot → poll.
- [x] Per-alarm signal (tone/blink/both): device confirmed that `mode blink`
  triggered only the backlight, not audio. Beep is now loud and backlight
  blinks.
- [x] Alarm app UI: tile (launcher 2×5), editor with numeric alarm-time entry
  (Alt-hold fix), signal field, and “+9 min” snooze button.
- [x] Mesh telemetry: setting, reading, and persisting `pos` confirmed on
  device.
- [x] End-to-end mesh GPS reception (06/17): Fennek “Daniel” sent
  `advert flood` at position 51.4818/7.2162; via relay/bridge it reached
  `meshcore.dumke.me`, where companion “Antonia” showed the node at the correct
  GPS pin (~2-minute latency). Only flood propagates; zero-hop does not reach
  the one-hop bridge. Battery telemetry is in non-standard FEAT fields, so
  the server does not decode voltage (position is the only standard advert
  field).
- [x] Remove the Fennek image credit: curved “by Dr. Daniel Dumke” text on the
  tail was too small, so it was removed and the original image restored. If
  desired, it could be larger or styled differently. Tools:
  `tools/sleepcredit.py` and `tools/sleepimg.py`.
- [x] Show battery percentage on standby screen with hourly update. Banner
  shows percentage at bottom right; wake sources are now the button and an
  hourly timer. On timer wake, `power::handleTimerWake()` uses a minimal path
  (no SD/audio/apps), refreshes only the banner strip (full-screen refresh
  every 12 wakes to prevent ghosting), then immediately sleeps again.
- [x] Web interface for SD file management: “Files” app (tile 7; launcher now
  2×4) plus `services/webfm`: Wi-Fi station, built-in `WebServer`, embedded
  HTML page, and JSON API (`/api/list|download|upload|delete|mkdir`; only
  `/.fennek` is read-only). Credentials are in Options (Wi-Fi SSID/password)
  or via serial console (`wifi ssid/pass`). Start in app or with
  `wifi start`; stop when leaving app. While Wi-Fi is active, audio stops and
  mesh pump pauses. mDNS: `http://fennek.local`.
- [x] MeshCore identity using a custom private/public key: loads
  `/meshcore/identity.hex` from SD (line 1 = 128-hex private key; line 2 =
  64-hex public key), takes precedence over SPIFFS, and is copied there
  (`mesh_client.cpp::begin`).
- [x] Book indexing was already implemented. Book list:
  `/.fennek/books.bin`; page index `/.fennek/idx/<crc>.idx` (magic + layout +
  file size); EPUB→TXT cache `/books/.epub_cache/`. Reopening uses the cache;
  only the initial scan is slow (inherent to the shared SPI bus).
- [x] Reduce excessive screen refreshes: reader progress now uses region
  refresh instead of full refresh; reader list avoids a no-op refresh at the
  top edge; forced anti-ghosting full refresh for Music/Audiobooks increased
  from ~30 s to ~60 s.
- [x] Improve Mesh app navigation: added a scrollable “Chats” view (all
  channels and DMs as rows); selecting one opens the conversation, and Back
  returns to the list. Channels are separated by chat (`MsgView.channelIdx`).
- [x] Podcast app crashed at startup.
  - **Done:** Feed array now uses heap allocation to avoid stack overflow on
    the default 8 KB task stack. `feedCount()` and `feed(idx)` were optimized
    so `feeds.txt` is read once at startup via `readFeeds()` instead of n+1
    times.
- [x] Use number keys in the mental-math app without pressing Alt.
  - **Done:** Added `mapKeyToDigit()` in `mathquiz_app.cpp`, mapping quiz
    screen letters (w,e,r,s,d,f,z,x,c) and microphone key (`0x02`) to digits
    1–9 and 0.
- [x] Remove the voice-notes menu item from Notes; use the keyboard microphone
  key instead.
  - **Done:** Reduced `kSpecialRows` from 2 to 1 (removed recording row from
    list). `onListKey()` intercepts microphone key (`0x02`) and calls
    `startRecord()`.
- [x] Audio recordings were difficult (slow, quiet, slow to save).
  - **Done:** Added an 8 KB RAM write buffer (`s_writeBuf`) in `mic.cpp` to
    block writes and greatly reduce SPI lock operations. Raised `kGain` to 12
    (+6 dB) for louder audio.
- [x] Mental-math zero key did not work (it was on Alt-microphone).
  - **Done:** Added an `s_altActive` check when handling microphone key (code
    34) in `keyboard.cpp`. Alt-microphone now correctly returns `0`.
- [x] Audio recordings did not play — are the WAV files valid?
  - **Done (diagnosed in code):** WAV header is correct (RIFF/WAVE, PCM,
    16 kHz mono 16-bit; RIFF/data sizes patched in `stopRecording`). Quiet
    recordings were addressed with recording gain ×12 (`mic.cpp`). I2S0
    handoff (`audio::beginMic/endMic` via `setI2SCommFMT_LSB`) is correct.
    `stopRecording` logs peak level; `audio::startCurrent` logs
    `connecttoFS` failure. Serial shows
    `[AUDIO] connecttoFS ERROR: <path>` if a file cannot be opened/parsed.
  - **Still needs device verification:** record a WAV, check the log (peak
    level >64?), then play it. If the speaker is silent, check peak in log and
    download the file through WebFM for testing on a PC with Audacity/`xxd`.

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// main.cpp — LilyGO T-Deck Pro: multi-app handheld (launcher + apps).
//
// Architecture:
//   Core 0: audio decode task (services/audio.cpp) — highest priority
//   Core 1: Arduino loop() — input polling + E-Ink UI (core/appmgr.cpp)
// Both share the SPI bus (E-Ink + SD); g_spiMutex serializes access.
// =============================================================================
#include <Arduino.h>
#include <Wire.h>
#include <SD.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <string.h>

#include "config.h"
#include "core/board.h"
#include "core/display.h"
#include "core/touch.h"
#include "core/keyboard.h"
#include "core/battery.h"
#include "core/settings.h"
#include "core/appmgr.h"
#include "core/console.h"
#include "core/power.h"
#include "services/library.h"
#include "services/audio.h"
#include "services/webfm.h"
#include "services/timesync.h"
#include "services/scrobble.h"
#include "services/podcast.h"
#include "services/calendar.h"
#include "services/reinschrift.h"
#include "services/alarmclock.h"
#include "services/settingsfile.h"
#include "services/battlog.h"
#include "apps/launcher.h"
#include "apps/music_app.h"
#include "apps/book_app.h"
#include "apps/reader_app.h"
#include "apps/notes_app.h"
#include "apps/mesh_app.h"
#include "apps/mesh_client.h"
#include "apps/settings_app.h"
#include "apps/games_app.h"
#include "apps/files_app.h"
#include "apps/alarms_app.h"
#include "apps/maps_app.h"
#include "apps/gyro_app.h"
#include "apps/mathquiz_app.h"
#include "apps/flashcards_app.h"
#include "apps/podcast_app.h"
#include "apps/calendar_app.h"
#include "apps/todo_app.h"

#ifdef GAMES_SMOKE_TEST
namespace {
// Send synthetic input to the active app and run one loop iteration (render dirty regions).
void gamesSmokeKey(char k) {
  InputEvent e; e.type = InputEvent::KEY; e.x = e.y = 0; e.key = k;
  appmgr::current()->handleInput(e);
  appmgr::loop();
}
void gamesSmokeTap(int16_t x, int16_t y) {
  InputEvent e; e.type = InputEvent::TAP; e.x = x; e.y = y; e.key = 0;
  appmgr::current()->handleInput(e);
  appmgr::loop();
}
}  // namespace
#endif

void setup() {
  Serial.begin(115200);

  // On an hourly timer wake from standby, use the minimal path: update only
  // the battery percentage on the sleep screen, then return to deep sleep.
  // Button wakes and cold starts continue through the normal setup path.
  power::handleTimerWake();

  // Allow USB CDC time to enumerate so the boot log is not lost.
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) delay(10);
  Serial.println("\n[FENNEK] Boot — Fennek " FENNEK_VERSION " (T-Deck Pro)");
  // Diagnose an immediate standby wake: why did we wake? (reset reason + wake source)
  Serial.printf("[FENNEK] Reset reason=%d wake source=%d ext1=0x%llx\n",
                (int)esp_reset_reason(), (int)esp_sleep_get_wakeup_cause(),
                (unsigned long long)esp_sleep_get_ext1_wakeup_status());

  // Crash-loop protection: after three consecutive abnormal resets (panic,
  // WDT, or brownout), enter emergency standby instead of draining the battery
  // in a boot loop. Run this before crash-prone components (SD, library, apps).
  power::noteBoot();

  // 1) Power and buses.
  board::powerOn();
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_HZ);
  board::initBus();
  Serial.println("[FENNEK] Power and buses OK");

  // 2) Initialize the display first so the user sees something immediately.
  display::begin();
  Serial.println("[FENNEK] Display init ok");

  // 3) Input, battery, and settings (none depend on the SPI bus).
  bool touchOk = touch::begin();
  Serial.printf("[FENNEK] Touch init: %s\n", touchOk ? "OK" : "FAILED");
  bool kbOk = keyboard::begin();
  Serial.printf("[FENNEK] Keyboard (TCA8418): %s\n", kbOk ? "OK" : "NOT FOUND");
  bool battOk = battery::begin();
  Serial.printf("[FENNEK] Battery (BQ27220): %s (%u mV, %u%%)\n",
                battOk ? "OK" : "NOT FOUND",
                battery::milliVolts(), battery::percent());
  settings::begin();
  // Time coordinator: restore the clock from NVS on cold start and load drift
  // and time-zone data. Run after settings::begin(); SD and audio are not needed.
  timesync::begin();

  // 4) SD and media library (the device continues without a card; apps show a notice).
  bool sdOk = board::initSD();
  Serial.printf("[FENNEK] SD card: %s\n", sdOk ? "mounted" : "NOT FOUND");
  if (sdOk) {
    // One-time migration: the cache directory was previously named /.meck (Meck era).
    spiLock();
    if (SD.exists("/.meck") && !SD.exists("/.fennek")) {
      bool ok = SD.rename("/.meck", "/.fennek");
      Serial.printf("[FENNEK] SD cache migration /.meck -> /.fennek: %s\n", ok ? "OK" : "FAILED");
    }
    spiUnlock();
    // Mirror settings from SD to NVS (SD is authoritative at boot) before
    // audio reads the volume and apps read Mesh/Wi-Fi settings.
    if (settingsfile::importFromSd())
      Serial.println("[FENNEK] Settings loaded from /fennek.ini");
  }
  library::begin();
  int n = 0;
  if (sdOk) {
    // Fast path: load the track cache from SD; otherwise scan in the background
    // in appmgr slices so boot is not blocked for minutes. The library itself
    // falls back to "/" when there is no content under /music.
    if (library::loadCache()) n = library::count();
    else                      library::startScan(MP3_DIR);
  }
  Serial.printf("[FENNEK] Tracks found: %d%s\n", n,
                library::scanning() ? " (scan running in background)" : "");

  // 5) Audio engine (starts the decode task on Core 0).
  audio::begin();
  if (settings::volume() != 255) audio::setVolume(settings::volume());
  Serial.printf("[FENNEK] Audio engine started (free PSRAM: %u KB)\n",
                (unsigned)(ESP.getFreePsram() / 1024));

  // Load pending scrobble entries from SD; requires SD and settings.
  scrobble::begin();

  // Podcast support is disabled (Wi-Fi sync is too slow, v2.5.9): /podcasts is
  // no longer created, and the app tile and auto-sync hook are disabled.
  // The code can be re-enabled.
  // podcast::begin();

  // Load caches and operation queues from SD for calendars (read-only iCal
  // feeds) and todos (Reinschrift/WebDAV). Sync runs on demand or before standby.
  calendar::begin();
  reinschrift_svc::begin();

  // Alarm engine: load alarms from NVS. Alarms use the audio queue, so initialize
  // this after audio::begin(); poll() runs in loop(). After an alarm wake,
  // power::handleTimerWake performs a full boot and poll() fires immediately.
  alarmclock::begin();

  // Debug battery logger (enabled only with -D BATTLOG): requires SD, battery,
  // and clock. Record the wake/boot reason as the first activity ("after standby").
  BATTLOG_BEGIN();
  BATTLOG_EVENT("Wake", "Reset=%d Wake=%d", (int)esp_reset_reason(),
                (int)esp_sleep_get_wakeup_cause());
  // Append sleep diagnostics from RTC RAM: whether the last sleep phase was
  // clean, plus the battery sample from each timer wake since the last full boot.
  power::logSleepDebug();
  // Flush to SD immediately after an abnormal reset (panic/WDT/brownout);
  // otherwise, a crash loop leaves no trace because the PSRAM ring is lost.
  {
    esp_reset_reason_t rr = esp_reset_reason();
    if (rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT ||
        rr == ESP_RST_WDT || rr == ESP_RST_BROWNOUT)
      BATTLOG_FLUSH("Crash boot");
  }

  Serial.printf("[FENNEK] Artists=%d Albums=%d Tracks=%d Playlists=%d\n",
                library::artistCount(), library::albumCount(),
                library::titleCount(), library::playlistCount());

#ifdef PLAYLIST_SELFTEST
  // TEMP: write a test .m3u (one absolute and one relative path), scan,
  // resolve the entries, and delete the file.
  if (sdOk && library::count() >= 2) {
    char p0[TRACK_PATH_LEN], p1[TRACK_PATH_LEN];
    library::path(0, p0, sizeof(p0));
    library::path(1, p1, sizeof(p1));
    const char* rel1 = strstr(p1, "/music/");
    spiLock();
    File w = SD.open("/music/__sel.m3u", FILE_WRITE);
    if (w) {
      w.println("#EXTM3U");
      w.println(p0);                          // absolute path
      w.println(rel1 ? rel1 + 7 : p1);        // relative to /music
      w.close();
    }
    spiUnlock();
    int nn = library::scan(MP3_DIR);
    Serial.printf("[SELF] rescan tracks=%d playlists=%d\n", nn, library::playlistCount());
    for (int i = 0; i < library::playlistCount(); i++) {
      char nm[64]; library::playlistName(i, nm, sizeof(nm));
      int c = library::playlistOpen(i);
      Serial.printf("[SELF] PL '%s' -> %d tracks\n", nm, c);
      for (int k = 0; k < c; k++) {
        char tn[64]; library::playlistTrackName(k, tn, sizeof(tn));
        Serial.printf("[SELF]    [%d] %s (flat %d)\n", k, tn, library::playlistTrackFlatIndex(k));
      }
    }
    spiLock(); SD.remove("/music/__sel.m3u"); spiUnlock();
    library::scan(MP3_DIR);  // restore a clean final state
  }
#endif

  // 6) Register apps: add the launcher first (Home), then populate its tiles.
  appmgr::add(launcher::get());
  appmgr::add(music_app::get());
  appmgr::add(book_app::get());
  appmgr::add(reader_app::get());
  appmgr::add(mesh_app::get());
  appmgr::add(settings_app::get());
  appmgr::add(games_app::get());
  appmgr::add(files_app::get());
  appmgr::add(notes_app::get());
  appmgr::add(alarms_app::get());
  appmgr::add(maps_app::get());
  // Location/gyro is disabled: do not register it or add a tile.
  // appmgr::add(gyro_app::get());
  appmgr::add(mathquiz_app::get());
  appmgr::add(flashcards_app::get());
  appmgr::add(calendar_app::get());
  appmgr::add(todo_app::get());
  // Podcast support is disabled (Wi-Fi sync is too slow, v2.5.9); do not
  // register the app or add a tile.
  // appmgr::add(podcast_app::get());
  launcher::setTile(0, i18n::Str::TileMusic,    music_app::get());
  launcher::setTile(1, i18n::Str::TileBook,     book_app::get());
  launcher::setTile(2, i18n::Str::TileReader,   reader_app::get());
  launcher::setTile(3, i18n::Str::TileMesh,     mesh_app::get());
  launcher::setTile(4, i18n::Str::TileSettings, settings_app::get());
  launcher::setTile(5, i18n::Str::TileGames,    games_app::get());
  launcher::setTile(6, i18n::Str::TileFiles,    files_app::get());
  launcher::setTile(7, i18n::Str::TileNotes,    notes_app::get());
  launcher::setTile(8, i18n::Str::TileAlarm,    alarms_app::get());
  launcher::setTile(9, i18n::Str::TileMaps,     maps_app::get());
  // Location/gyro is disabled: omit its tile and move page 2 up.
  // launcher::setTile(10, i18n::Str::TileGyro, gyro_app::get());
  launcher::setTile(10, i18n::Str::TileMath,     mathquiz_app::get());   // Page 2, slot 0
  launcher::setTile(11, i18n::Str::TileCards,    flashcards_app::get());
  launcher::setTile(12, i18n::Str::TileCalendar, calendar_app::get());
  launcher::setTile(13, i18n::Str::TileTodo,     todo_app::get());
  // Podcast is disabled (v2.5.9), but the code can be re-enabled.
  // launcher::setTile(15, i18n::Str::TilePodcast, podcast_app::get());
  appmgr::begin();
  Serial.println("[FENNEK] Setup complete — launcher running.");

#ifdef GAMES_SMOKE_TEST
  // TEMP: click through all five games once (draw paths + chess AI task).
  Serial.println("[GAME] SMOKE: Start");
  appmgr::launch(games_app::get());
  appmgr::loop();                  // render the games menu
  gamesSmokeKey('\r');             // open 2048 (selection 0)
  gamesSmokeKey('a');
  gamesSmokeKey('s');
  gamesSmokeKey('d');
  gamesSmokeKey('w');
  gamesSmokeKey('\b');             // return to the menu
  gamesSmokeKey('s');              // -> Minesweeper
  gamesSmokeKey('\r');
  gamesSmokeTap(120, 170);         // reveal the center cell (flood fill)
  gamesSmokeKey('f');              // enable flag mode
  gamesSmokeTap(12, 64);           // flag cell 0
  gamesSmokeKey('f');
  gamesSmokeKey('\b');
  gamesSmokeKey('s');              // -> chess
  gamesSmokeKey('\r');             // setup dialog
  gamesSmokeKey('s');              // "Color" row
  gamesSmokeKey('d');              // -> black (Fennek is white and moves first)
  gamesSmokeKey('s');
  gamesSmokeKey('s');              // select "Start"
  gamesSmokeKey('\r');             // start the game; AI thinks
  {
    uint32_t t0 = millis();
    while (millis() - t0 < 20000) { appmgr::loop(); delay(10); }
  }
  gamesSmokeKey('\b');             // return (saves the game to NVS)
  gamesSmokeKey('s');              // -> Tic-Tac-Toe
  gamesSmokeKey('\r');
  gamesSmokeTap(48, 100);          // place a mark in cell 0; Fennek responds
  gamesSmokeKey('\b');             // return to the menu
  gamesSmokeKey('s');              // -> Sudoku
  gamesSmokeKey('\r');             // generate a puzzle (solver/generator)
  gamesSmokeKey('d');              // move the cursor
  gamesSmokeKey('s');
  gamesSmokeKey('5');              // set a digit (if the cell is not fixed)
  gamesSmokeKey('0');              // clear the cell
  gamesSmokeKey('m');              // change difficulty -> generate a new puzzle
  gamesSmokeKey('\b');             // return to the menu
  Serial.printf("[GAME] SMOKE: complete — free heap %u KB\n",
                (unsigned)(ESP.getFreeHeap() / 1024));
  appmgr::goHome();
  appmgr::loop();
#endif

#ifdef MESH_SMOKE_TEST
  // TEMP: verify radio bring-up without UI interaction (serial log).
  Serial.println("[FENNEK] MESH_SMOKE_TEST: initializing Mesh ...");
  bool meshOk = mesh_client::begin();
  Serial.printf("[FENNEK] MESH_SMOKE_TEST: %s (freier Heap: %u KB)\n",
                meshOk ? "OK" : "FAILED",
                (unsigned)(ESP.getFreeHeap() / 1024));
#endif

#ifdef AUDIO_DEBUG_GAP
  // TEMP: play track 0 and force a refresh every 2 seconds. The audio task
  // reports [GAP] maxGap; a low value despite refreshes means playback is smooth.
  if (n > 0) {
    char p0[TRACK_PATH_LEN];
    library::path(0, p0, sizeof(p0));
    audio::queueBegin(audio::Owner::Music);
    audio::queueAdd(p0);
    audio::queueCommit(0);
    for (int i = 0; i < 12; i++) {
      delay(2000);
      Serial.printf("[FENNEK] >>> forcing display refresh #%d <<<\n", i + 1);
      display::render([](Adafruit_GFX& g) {
        g.setTextSize(2); g.setCursor(10, 150); g.print("REFRESH-TEST");
      }, false);
    }
  }
#endif

  // Finally, enable the 80 MHz CPU base clock (boot ran at 240 MHz). From here,
  // audio, Wi-Fi, chess, EPUB, and the console request 240 MHz as needed (power.h).
  power::boostBegin();
}

void loop() {
  console::poll();    // Serial debug console ('help' over USB)
  webfm::poll();      // Wi-Fi state machine + HTTP requests (no-op when off)
  timesync::poll();   // Keep time current (opportunistic NTP, NVS backup)
  scrobble::poll();   // Persist the scrobble queue to SD at a limited rate
  alarmclock::poll(); // Check alarm deadlines and ring
  BATTLOG_POLL();     // Debug battery logger (no-op without -D BATTLOG)
  appmgr::loop();

  // Adaptive input polling: stay near 100 Hz while activity is occurring
  // (input, active playback keeps power::idleMs() near 0, or Wi-Fi is handling
  // HTTP requests). After a few seconds idle, slow to about 30 Hz to reduce
  // Core 1 wake time without making touch or keyboard feel sluggish (key repeat
  // and debounce use millis(); audio runs on Core 0). Only applies while awake,
  // before automatic standby.
  const bool busy = power::idleMs() < 3000 || webfm::state() != webfm::State::OFF;
  delay(busy ? 10 : 33);
}

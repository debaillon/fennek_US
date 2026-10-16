// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// settings.h — NVS persistence (Preferences).
//
// NVS is stored in internal QSPI flash. Writes NEVER touch the shared HSPI
// bus, so they do not contend with audio streaming or E-Ink refresh. Writes
// are atomic and wear-leveled; only write values when they change.
//
// Bulk data (ID3 cache, page indexes) belongs on the SD card, not in the
// 20 KB NVS partition.
// =============================================================================
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace settings {

void begin();

// --- General (namespace "fennek"; migrated once from the old "meck") --------
uint8_t volume();
void    setVolume(uint8_t v);          // writes only when changed

void    lastApp(char* out, size_t n);  // Last active app name ("" = none)
void    setLastApp(const char* name);

// Automatic standby after inactivity (minutes; 0 = off). Default: 5.
uint8_t standbyMinutes();
void    setStandbyMinutes(uint8_t m);  // writes only when changed

// Legacy language setting retained for source and file-format compatibility.
// The runtime UI is English-only; language() always returns 0 and setLanguage
// ignores its argument.
uint8_t language();
void    setLanguage(uint8_t lang);

// Font size for books, text files, and the notes editor: 1 = small (default),
// 2 = large. Uses the classic 6x8 font with integer scaling.
uint8_t fontScale();
void    setFontScale(uint8_t s);       // 1..2; writes only when changed

// Put the E-Ink controller into deep sleep before standby (command 0x07 to the
// UC8253). This saves a few µA, but waking requires a wired RST line. The
// T-Deck Pro V1.0 has no RST line (LilyGO defines BOARD_EPD_RST as -1 there),
// so the panel remains asleep until its charge dissipates (GitHub issue #2).
// Disabled by default: powerOff turns off only the booster before sleep; the
// image itself draws no power. Console: `eink hib on|off`.
bool    einkHibernate();               // Default: false
void    setEinkHibernate(bool on);     // writes only when changed

// --- Music: last played track (for resume) -----------------------------------
// Path and position; an empty path means nothing is saved.
void    lastTrack(char* pathOut, size_t n, uint32_t* posSec);
void    setLastTrack(const char* path, uint32_t posSec);
void    clearLastTrack();

// --- Audiobook bookmarks (namespace "abook", key = CRC32 of the book folder) -
bool    bookBookmark(uint32_t key, uint16_t* fileIdx, uint32_t* posSec);
void    setBookBookmark(uint32_t key, uint16_t fileIdx, uint32_t posSec);
void    clearBookBookmark(uint32_t key);

// --- eBook reading positions (namespace "ebook", key = CRC32 of the path) ----
bool    readPos(uint32_t key, uint32_t* page);
void    setReadPos(uint32_t key, uint32_t page);

// Most recently opened book (full path; "" = none). On first entry after boot,
// the reader opens this book at the page saved by readPos.
void    lastBook(char* out, size_t n);
void    setLastBook(const char* path);

// --- Mesh radio parameters ----------------------------------------------------
// Defaults to "EU/UK Narrow": 869.618 MHz, 62.5 kHz bandwidth, SF 8, CR 4/5,
// 22 dBm. The Settings app also provides EU and US 915 presets. Changes are
// applied to the radio live by mesh_client::applyRadioParams().
struct MeshParams {
  float   freqMhz;
  float   bwKhz;
  uint8_t sf;        // 7..12
  uint8_t cr;        // 5..8 (4/x)
  uint8_t txDbm;     // 1..22
};
MeshParams meshParams();
void       setMeshParams(const MeshParams& p);   // writes only changes

// RX power-saving mode: SX1262 duty-cycle reception (the radio sleeps between
// short listening windows and wakes on the preamble; saves about two-thirds
// of continuous RX power). Transmitters on the network must use at least our
// preamble length (the current MeshCore default). Console: `mesh eco on|off`.
bool meshEco();                                  // Default true
void setMeshEco(bool on);

void meshName(char* out, size_t n);              // Node name (default: "T-Deck")
void setMeshName(const char* name);

// Node position in decimal degrees for location beacons in the advert.
// 0/0 = unset (treated as unknown and omitted from the advert).
void meshPos(double* lat, double* lon);
void setMeshPos(double lat, double lon);         // writes only changes

// --- Wi-Fi credentials --------------------------------------------------------
// Multiple known networks (profiles). When connecting, services/wifi selects
// the strongest known network in range (scan match). Slot 0 is also the
// "simple" Wi-Fi setting used by INI export and the WebFM display.
constexpr int kMaxWifiProfiles = 20;

int  wifiCount();                                // Number of configured profiles (0..kMaxWifiProfiles)
void wifiSsidAt(int i, char* out, size_t n);     // "" if i is invalid
void wifiPassAt(int i, char* out, size_t n);
// Set profile i. If i == wifiCount(), append a new profile. An empty SSID
// removes the profile (same as wifiRemove). Returns true if anything changed.
bool wifiSet(int i, const char* ssid, const char* pass);
void wifiRemove(int i);

// Legacy/simple accessors use slot 0 (backward compatibility).
void wifiSsid(char* out, size_t n);              // "" = not configured
void setWifiSsid(const char* ssid);
void wifiPass(char* out, size_t n);
void setWifiPass(const char* pass);

// --- Navidrome/Subsonic scrobbling (services/scrobble) ------------------------
// Server URL including scheme (http/https), username, and password (plain text,
// as with Wi-Fi). The toggle enables tracking and pre-standby uploads of tracks.
bool navEnabled();                               // Default false
void setNavEnabled(bool on);
void navUrl(char* out, size_t n);                // "" = not configured
void setNavUrl(const char* url);
void navUser(char* out, size_t n);
void setNavUser(const char* user);
void navPass(char* out, size_t n);
void setNavPass(const char* pass);

// --- Ollama AI for notes (services/notes_ai) ----------------------------------
// Server URL including scheme and port (e.g. http://192.168.1.10:11434) and
// model name (e.g. "llama3.2"). The toggle enables automatic note polishing
// before standby when Wi-Fi is available.
bool aiEnabled();                                // Default false
void setAiEnabled(bool on);
void aiUrl(char* out, size_t n);                 // "" = not configured
void setAiUrl(const char* url);
void aiModel(char* out, size_t n);               // Default "llama3.2"
void setAiModel(const char* model);

// --- OTA firmware updates (services/ota) --------------------------------------
// Manifest URL: GitHub Releases API by default, or a self-hosted manifest.
// Default: latest release from danst0/fennek. "" causes OTA checks to fail.
void otaUrl(char* out, size_t n);
void setOtaUrl(const char* url);

// --- Podcast (services/podcast) ------------------------------------------------
// Toggle for automatic episode sync BEFORE automatic standby when Wi-Fi is
// available. Manual sync in the Podcast app works independently.
bool podcastAutoSync();                          // Default false
void setPodcastAutoSync(bool on);

// --- Todo/Reinschrift (services/reinschrift) ----------------------------------
// Nextcloud WebDAV: base URL including scheme, username, and password (plain
// text, as with Wi-Fi), plus the Markdown database path relative to the WebDAV
// root (e.g. "Projekte/TodosDatenbank.md"). autoSync syncs before automatic standby.
bool todoEnabled();                              // Default false
void setTodoEnabled(bool on);
void todoUrl(char* out, size_t n);               // "" = not configured
void setTodoUrl(const char* url);
void todoUser(char* out, size_t n);
void setTodoUser(const char* user);
void todoPass(char* out, size_t n);
void setTodoPass(const char* pass);
void todoPath(char* out, size_t n);              // WebDAV path to the .md file
void setTodoPath(const char* path);
bool todoAutoSync();                             // Default false
void setTodoAutoSync(bool on);

// --- Calendar (services/calendar) ---------------------------------------------
// iCal feeds are listed in /calendar/feeds.txt. This stores the auto-sync
// toggle (sync before automatic standby) and optional CalDAV credentials
// (Apple ID and app-specific password) for `caldav:` feeds (e.g. iCloud).
// Plain `.ics` feeds remain unauthenticated.
bool calAutoSync();                              // Default false
void setCalAutoSync(bool on);
void calDavUser(char* out, size_t n);            // "" = no authentication
void setCalDavUser(const char* user);
void calDavPass(char* out, size_t n);
void setCalDavPass(const char* pass);

// --- Calibre book sync (services/calibre_books) --------------------------------
// Calibre-Web URL including scheme (e.g. https://calibre.dumke.me), Basic Auth
// credentials (Calibre-Web login; OPDS always uses Basic Auth), and the shelf
// that determines which books are downloaded to the device (name or numeric
// shelf ID; default: "Fennek"). autoSync pulls before standby, at most every 6 h.
void calibreUrl(char* out, size_t n);            // "" = not configured
void setCalibreUrl(const char* url);
void calibreUser(char* out, size_t n);           // "" = no authentication
void setCalibreUser(const char* user);
void calibrePass(char* out, size_t n);
void setCalibrePass(const char* pass);
void calibreShelf(char* out, size_t n);          // Default: "Fennek"
void setCalibreShelf(const char* shelf);
bool calibreAutoSync();                          // Default false
void setCalibreAutoSync(bool on);

// --- Time and time zone (services/timesync) -----------------------------------
uint32_t lastTime();                      // 0 = never saved (cold-start fallback)
void     setLastTime(uint32_t epoch);
uint16_t clockPpm();                      // Learned oscillator drift; 0 = unknown
void     setClockPpm(uint16_t ppm);
void     tzString(char* out, size_t n);   // POSIX TZ (default: Europe/Berlin)
void     setTzString(const char* tz);

// --- Games: high scores and saved chess game (namespace "fennek") -------------
// Write only when a game ends or is exited (chess blob).
uint32_t best2048();                       // 0 = no score yet
void     setBest2048(uint32_t score);      // writes only for a new high score
uint16_t minesWins();
uint16_t minesBestSec();                   // 0 = no win yet
void     setMinesResult(bool won, uint16_t sec);
uint16_t chessWins();                      // Wins against the device
void     addChessWin();
bool     chessGame(void* buf, size_t n);   // Load an ongoing game (true = found)
void     setChessGame(const void* buf, size_t n);   // n=0 deletes
uint16_t tttWins();
uint16_t tttDraws();
void     addTttResult(bool win, bool draw);
uint16_t sudokuSolved();                   // Number of solved puzzles
uint16_t sudokuBestSec();                  // 0 = none solved yet
void     setSudokuResult(bool won, uint16_t sec);
uint16_t mathBestStreak();                 // Mental math: longest answer streak
void     setMathBestStreak(uint16_t streak);   // writes only for a new high score
uint8_t  mathLevel(uint8_t mode);          // Mental math: adaptive level per mode (0..2)
void     setMathLevel(uint8_t mode, uint8_t level);   // writes only when the level changes

// CRC32 helper (cache and bookmark keys).
uint32_t crc32(const char* s);

// --- INI export/import (SD mirror, services/settingsfile) ---------------------
// Intentionally SPI-free: both functions use only a RAM buffer; SD I/O is in
// services/settingsfile.* (under spiLock). exportIni() serializes all values;
// importIni() parses them back into NVS. Per-book bookmarks and reading
// positions are keyed by CRC32 (the path cannot be recovered), so they are
// NOT included.
size_t exportIni(char* out, size_t cap);   // Returns the number of bytes written
int    importIni(const char* text);        // Applied keys; -1 = parse error

}  // namespace settings

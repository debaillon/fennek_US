// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

#include "battlog.h"

// The entire translation unit is gated. Without -D BATTLOG, no functions or
// symbols are created (callers expand to ((void)0)).
#ifdef BATTLOG

#include "config.h"
#include "core/battery.h"
#include "core/board.h"
#include "services/timesync.h"
#include "services/webfm.h"
#include "apps/mesh_client.h"

#include <Arduino.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace {

constexpr int      kRingMax     = 256;            // PSRAM ring (FIFO)
const char*        kFile        = "/.fennek/battery.log";
const char*        kFileOld     = "/.fennek/battery.old";
constexpr uint32_t kSampleMs    = 60000;          // periodic battery sample
constexpr uint32_t kFlushMs     = 120000;         // flush to SD at least every 2 min
constexpr int      kFlushAt     = 200;            // or when this many lines are pending
constexpr uint32_t kMaxFileSize = 256UL * 1024;   // rotation (like the mesh log)
constexpr uint32_t kSaneEpoch   = 1500000000UL;   // clock is invalid below this value

// Flag bits in the Entry.flags field.
constexpr uint8_t F_CHG  = 1;   // actively charging (average current > 0)
constexpr uint8_t F_WIFI = 2;   // Wi-Fi (webfm) on
constexpr uint8_t F_MESH = 4;   // mesh radio initialized (chip on)
constexpr uint8_t F_EXT  = 8;   // connected to external power, see below

struct Entry {
  uint32_t epoch;     // UTC; 0/ungültig = Uhr noch nicht gesetzt
  uint16_t mv;        // Akkuspannung
  uint16_t rm;        // RemainingCapacity (mAh, Coulomb-Counter)
  uint8_t  pct;       // Ladestand %
  uint8_t  flags;     // F_CHG | F_WIFI | F_MESH | F_EXT
  char     cat[12];   // category (Boot/App/Audio/Mesh/Wi-Fi/Standby/Wake/Battery/...)
  char     text[64];  // note
};

Entry*            s_ring   = nullptr;   // PSRAM
int               s_count  = 0;
SemaphoreHandle_t s_mutex  = nullptr;
uint32_t          s_lastSampleMs = 0;
uint32_t          s_lastFlushMs  = 0;

// Cached battery values: only poll() (Core 1) reads the I2C gauge; event() uses
// the cache so it can be called from any task, including the audio task, without
// competing for the I2C bus.
volatile uint16_t s_cMv  = 0;
volatile uint8_t  s_cPct = 0;
volatile uint16_t s_cRm  = 0;
volatile bool     s_cChg = false;
volatile bool     s_cExt = false;
bool              s_haveCharge = false;   // whether charging status has been seen

uint8_t sysFlags(bool chg, bool ext) {
  uint8_t f = 0;
  if (chg)                                 f |= F_CHG;
  if (ext)                                 f |= F_EXT;
  if (webfm::state() != webfm::State::OFF) f |= F_WIFI;
  if (mesh_client::ready())                f |= F_MESH;   // beide nur Flag-Reads
  return f;
}

// Add an entry to the ring (FIFO; discard the oldest entry when full). Task-safe.
void append(const char* cat, const char* text) {
  if (!s_ring) return;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (s_count >= kRingMax) {
    memmove(&s_ring[0], &s_ring[1], sizeof(Entry) * (kRingMax - 1));
    s_count = kRingMax - 1;
  }
  Entry& e = s_ring[s_count++];
  e.epoch = timesync::now();
  e.mv    = s_cMv;
  e.rm    = s_cRm;
  e.pct   = s_cPct;
  e.flags = sysFlags(s_cChg, s_cExt);
  strncpy(e.cat,  cat,  sizeof(e.cat)  - 1); e.cat[sizeof(e.cat)   - 1] = '\0';
  strncpy(e.text, text, sizeof(e.text) - 1); e.text[sizeof(e.text) - 1] = '\0';
  xSemaphoreGive(s_mutex);
}

// Build one tab-separated SD row. Columns:
//   time | % | mV | mAh | power source | W: | M: | category | note
//
// The power-source field has three possible values. Previously it only recorded
// charging() (average current > 0) as "charging" or "-". When the battery is
// full while plugged in, the charger stops and current falls to about zero, so
// the field showed "-" even though USB was connected and the battery was not
// being drained. Those periods looked like perfect standby in the log and
// skewed analysis. "external()" (the DSG bit) now makes this explicit: only rows
// marked "-" are true battery measurements.
void formatLine(const Entry& e, char* out, size_t n) {
  char ts[24];
  if (e.epoch >= kSaneEpoch) {
    time_t t = (time_t)e.epoch;
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);
  } else {
    strncpy(ts, "0000-00-00 00:00:00", sizeof(ts));
    ts[sizeof(ts) - 1] = '\0';
  }
  snprintf(out, n, "%s\t%u%%\t%umV\t%umAh\t%s\tW:%s\tM:%s\t%s\t%s\n",
           ts, (unsigned)e.pct, (unsigned)e.mv, (unsigned)e.rm,
           (e.flags & F_CHG)  ? "charging" : (e.flags & F_EXT) ? "external" : "-",
           (e.flags & F_WIFI) ? "on"       : "off",
           (e.flags & F_MESH) ? "on"       : "off",
           e.cat, e.text);
}

// Append the ring to SD and clear it. Only called on Core 1 (poll/flush), under spiLock.
void writeOut() {
  if (!s_ring || !board::sdReady()) return;

  // Take a snapshot under the mutex, then perform SD I/O without holding it.
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  int n = s_count;
  Entry* snap = nullptr;
  if (n > 0) {
    snap = (Entry*)malloc(sizeof(Entry) * n);
    if (snap) memcpy(snap, s_ring, sizeof(Entry) * n);
    else n = 0;
  }
  s_count = 0;   // ring cleared (append to SD)
  xSemaphoreGive(s_mutex);
  if (n == 0) return;

  spiLock();
  SD.mkdir("/.fennek");
  // Rotate an oversized file to .old, then start a fresh log.
  File chk = SD.open(kFile, FILE_READ);
  uint32_t sz = chk ? chk.size() : 0;
  if (chk) chk.close();
  if (sz > kMaxFileSize) {
    SD.remove(kFileOld);
    SD.rename(kFile, kFileOld);
  }
  File f = SD.open(kFile, FILE_APPEND);
  if (f) {
    char line[176];
    for (int i = 0; i < n; i++) {
      formatLine(snap[i], line, sizeof(line));
      f.print(line);
    }
    f.close();
  }
  spiUnlock();
  free(snap);
  Serial.printf("[BATTLOG] Wrote %d lines to %s\n", n, kFile);
}

}  // namespace

namespace battlog {

void begin() {
  s_mutex = xSemaphoreCreateMutex();
  s_ring  = (Entry*)heap_caps_malloc(sizeof(Entry) * kRingMax, MALLOC_CAP_SPIRAM);
  if (!s_ring) s_ring = (Entry*)malloc(sizeof(Entry) * kRingMax);
  s_count = 0;
  // Sample first so the boot row includes the actual battery level.
  s_cMv  = battery::milliVolts();
  s_cPct = battery::percent();
  s_cRm  = battery::remainingCapacity();
  s_cChg = battery::charging();
  s_cExt = battery::external();
  s_haveCharge   = true;
  s_lastSampleMs = s_lastFlushMs = millis();
  append("Boot", "Fennek " FENNEK_VERSION);
  Serial.printf("[BATTLOG] active (debug) -> %s\n", kFile);
}

void event(const char* cat, const char* fmt, ...) {
  if (!s_ring) return;
  char buf[64];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  append(cat, buf);
}

void poll() {
  if (!s_ring) return;
  uint32_t now = millis();

  // Periodic battery sample (reads I2C, only here on Core 1).
  if (now - s_lastSampleMs >= kSampleMs) {
    s_lastSampleMs = now;
    s_cMv  = battery::milliVolts();
    s_cPct = battery::percent();
    s_cRm  = battery::remainingCapacity();
    bool chg     = battery::charging();
    bool ext     = battery::external();
    bool prevChg = s_cChg;
    bool prevExt = s_cExt;
    s_cChg = chg;
    s_cExt = ext;
    if (s_haveCharge && chg != prevChg) append("Charging", chg ? "started" : "stopped");
    // Mark external power transitions as events, not just as a column value.
    if (s_haveCharge && ext != prevExt) append("External power", ext ? "connected" : "disconnected");
    s_haveCharge = true;
    append("Battery", "Sample");
  }

  // Write to SD periodically (or when the ring is nearly full).
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  int cnt = s_count;
  xSemaphoreGive(s_mutex);
  if (cnt >= kFlushAt || (cnt > 0 && now - s_lastFlushMs >= kFlushMs)) {
    s_lastFlushMs = now;
    writeOut();
  }
}

void flush(const char* reason) {
  if (!s_ring) return;
  if (reason && reason[0]) append("Flush", reason);
  s_lastFlushMs = millis();
  writeOut();
}

}  // namespace battlog

#endif  // BATTLOG

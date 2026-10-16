// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// battlog.h — Debug battery/activity logger (only with -D BATTLOG).
//
// Purpose: understand battery consumption. Writes one line per event and a
// periodic battery sample every 60 s, each with a timestamp, battery level
// (%, mV, mAh, power source), system context (Wi-Fi on/off, mesh chip on/off),
// and a category + note (app switch, audio track, standby/wake, etc.). The ring
// buffer resides in PSRAM; writes are throttled and flushed to
// /.fennek/battery.log shortly before standby at the latest (downloadable via WebFM).
//
// Analysis — two rules that have tripped up previous analysis:
//  1. ONLY lines with "-" in the power-source column are real battery
//     measurements. "Cable" means USB is connected, the battery is full, and
//     charging has stopped; it may look like exceptionally low consumption
//     but is not a battery measurement.
//  2. Use the mAh column for consumption, not % or mV: it is the gauge's
//     Coulomb counter (actual charge integration), while percent() is only
//     estimated from the voltage curve. mA = Δ mAh * 3600 / Δ seconds.
//     Deep-sleep current is reported by the "sleep" lines from
//     power::logSleepDebug().
//
// Intentionally built as an optional debug feature: without the BATTLOG
// compile flag, all BATTLOG_* calls compile to no-ops (the .cpp is entirely
// gated). To remove it permanently, remove the flag, then delete these two
// files and the BATTLOG_* calls at their call sites.
//
// Threading: event() can be called from ANY task (including the audio task): it
// reads only cached battery values, flags, and time, then writes to the ring
// under a mutex; it performs no I2C/SPI access. poll()/flush() run ONLY in the
// Arduino loop (Core 1) and perform SD I/O under spiLock.
// =============================================================================
#pragma once

#ifdef BATTLOG
namespace battlog {

// Allocate the PSRAM ring, take the first battery sample, and write the boot line.
void begin();

// Call in Arduino-loop(): sample the battery every 60 s and write to SD, throttled.
void poll();

// Queue an event with a timestamp and current battery/system snapshot.
// fmt/... follows printf conventions (e.g. event("App", "%s", id)).
void event(const char* cat, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

// Flush the ring to SD immediately (before standby). reason is written as a line.
void flush(const char* reason);

}  // namespace battlog

#define BATTLOG_BEGIN()         battlog::begin()
#define BATTLOG_POLL()          battlog::poll()
#define BATTLOG_EVENT(cat, ...) battlog::event((cat), __VA_ARGS__)
#define BATTLOG_FLUSH(reason)   battlog::flush((reason))
#else
#define BATTLOG_BEGIN()         ((void)0)
#define BATTLOG_POLL()          ((void)0)
#define BATTLOG_EVENT(cat, ...) ((void)0)
#define BATTLOG_FLUSH(reason)   ((void)0)
#endif

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// timesync.h — Time coordinator (the device has no battery-backed hardware RTC).
//
// The canonical clock is the ESP32 system time (settimeofday/gettimeofday).
// It survives deep sleep because the RTC counter keeps running, and is updated
// from two sources:
//   1. NTP over Wi-Fi — opportunistically while Wi-Fi (webfm) is already on,
//      and once before automatic standby if estimated quality is poor and
//      Wi-Fi credentials are configured.
//   2. Mesh advertisements — reported by mesh_client through onExternalSync().
//
// Sync frequency depends on estimated clock quality (estimated error equals
// learned oscillator drift multiplied by time since the last sync), not a fixed
// interval. NVS stores the last time and learned drift only as a cold-start
// fallback after power loss, reset, or reflashing.
// =============================================================================
#pragma once

#include <stdint.h>

namespace timesync {

// At boot (after settings::begin()), restore the clock from NVS if the system
// time is still in its cold-start state, and load the learned drift.
void begin();

// Per loop iteration (alongside webfm::poll()), sync opportunistically over NTP
// while Wi-Fi is on and save to NVS at a limited rate.
void poll();

// Blocking; called only from the automatic-standby path (power::poll) before
// sleep. If quality is poor, Wi-Fi credentials are configured, and webfm is
// inactive, briefly turn on Wi-Fi, sync over NTP, and turn it back off.
// Otherwise this does nothing.
void syncBeforeStandby();

// Also called only from the automatic-standby path, before syncBeforeStandby().
// If the clock is stale, briefly start GPS (with aiding), wait for a valid
// position fix (about a 30-second timeout, with RTC back-off on failure), and
// adopt satellite-accurate UTC without Wi-Fi. If successful, the following NTP
// sync is a no-op through gpsFresh(). Returns true if GPS time was adopted.
// Does nothing if the clock is already good.
bool gpsSyncBeforeStandby();

// Called by mesh_client when an advertisement sets the clock (freshness).
void onExternalSync(uint32_t epoch, const char* src);

// Report GPS UTC (RMC from the Maps app). Highest priority: sets the clock and
// suppresses NTP/mesh as a source for about 10 minutes. Does nothing for an
// implausible epoch or a difference of <=2 seconds (only updates freshness).
void gpsSync(uint32_t epochUtc);

// Apply the time zone (settings::tzString) to libc (setenv TZ + tzset) so
// localtime_r() returns local time. Call from begin() and after settings change.
void applyTimezone();

// Set the clock manually (UTC epoch). This is a confirmed sync and marks the
// clock as good. Used by the Settings app/console when Wi-Fi and mesh are
// unavailable.
void setManualTime(uint32_t epochUtc);

// Force an immediate NTP sync (blocking, starts its own Wi-Fi connection);
// console command: `time sync`. Ignores quality and back-off logic.
// Returns false if Wi-Fi or credentials are unavailable, or if sync fails.
bool forceSyncNow();

// --- Queries -----------------------------------------------------------------
uint32_t    now();              // current time (Unix epoch, UTC)
uint32_t    estErrSeconds();    // estimated error in seconds
bool        isStale();          // estErrSeconds() over the threshold (~5 min)?
const char* source();           // "NTP" / "Mesh" / "NVS" / "—"
const char* qualityStr();       // "good" / "fair" / "poor"
uint16_t    driftPpm();         // learned oscillator drift

}  // namespace timesync

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// alarmclock.h — Alarm engine (independent of UI; console + app).
//
// The device has no hardware RTC, but ESP32 system time survives deep sleep
// (services/timesync). The alarm uses this: power::enterStandby() queries
// nextDueEpoch() and sets a timer wake for the next alarm time (instead of a
// fixed 1 h); power::handleTimerWake() detects the alarm wake and fully boots
// (SD + audio) so poll() can ring.
//
// Ringing uses the normal audio queue (Owner Music, Repeat One). If no ringtone
// path is set, the first library track plays.
//
// Persistence: dedicated NVS namespace "alarm" (internal flash, never SPI).
// Alarms use fixed slots; dowMask = 0 means daily, otherwise weekdays
// (bit0=Mon).
// =============================================================================
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace alarmclock {

constexpr int kMaxAlarms = 4;

// Signal mode when ringing: bitmask for tone/blink/vibration (3 bits per alarm).
constexpr uint8_t SIG_TONE  = 1;
constexpr uint8_t SIG_BLINK = 2;
constexpr uint8_t SIG_VIBRA = 4;
constexpr uint8_t SIG_BOTH  = 3;   // Tone + blink (default)

struct Alarm {
  bool    enabled;
  uint8_t hour;      // 0..23 (local time)
  uint8_t minute;    // 0..59
  uint8_t dowMask;   // bit0=Mon .. bit6=Sun; 0 = daily
  uint8_t signal;    // SIG_TONE/BLINK/BOTH (1..3); 0 is read as BOTH
  bool    once;      // One-time: fires at the next hh:mm (any day) then disables itself
};

void begin();        // Load alarms from NVS
void poll();         // In loop(): check due time, ring, auto-dismiss

int   count();                       // = kMaxAlarms (fixed slots)
Alarm get(int i);
void  set(int i, const Alarm& a);    // Persist immediately
void  clear(int i);                  // Disable slot

// Next trigger time (UTC epoch) >= fromEpoch across all active alarms
// (including ongoing snooze); 0 if none are active. Used for wake calculation
// in power.cpp. Uses mktime/localtime, so a valid time zone is required.
uint32_t nextDueEpoch(uint32_t fromEpoch);

bool ringing();
uint32_t snoozeMinutes();   // Snooze duration (for button label)
void fireNow();      // Ring immediately (console test "alarm test")
void snooze();       // Snooze (+kSnoozeMinutes), stop ringing
void dismiss();      // Dismiss, stop ringing

void soundPath(char* out, size_t n);  // "" = first library track
void setSoundPath(const char* p);

}  // namespace alarmclock

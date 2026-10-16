// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// settingsfile.h — Mirror settings to /fennek.ini on the SD card.
//
// Thin SD layer over settings::exportIni()/importIni(): reads/writes the file
// under spiLock() (same discipline as webfm); serialization/deserialization
// itself remains SPI-free in settings.cpp. Both quietly return false without an SD card.
//
// Boot: main.cpp calls importFromSd() after mounting the SD card (SD is authoritative).
// Standby: power::enterStandby() calls exportToSd() to keep the file current.
// Console: 'settings save'/'load'.
// =============================================================================
#pragma once

namespace settingsfile {

// Write all settings to /fennek.ini. false = no SD card or an error.
bool exportToSd();

// Read /fennek.ini back into NVS. false = no SD card, file missing, or error.
bool importFromSd();

}  // namespace settingsfile

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// notes_ai.h — Automatically polish notes with Ollama (local LLM).
//
// Background (CLAUDE.md): Wi-Fi and audio are mutually exclusive on this
// hardware (the Wi-Fi task on Core 0 displaces the audio task, and both share
// the SPI bus). There is no persistent Wi-Fi connection, so the established
// "Wi-Fi available" window is the pre-standby run (like
// scrobble::flushBeforeStandby() and timesync::syncBeforeStandby()).
//
// Flow: before automatic standby, scan all daily notes (/notes/*.md) that have
// changed since the last polish (hash comparison with /.fennek/notes_ai.state).
// Skip today's note so a note that is still growing is not rewritten during
// the day. Send each note to Ollama's /api/generate with HTTP POST (stream:false)
// and replace it directly on the SD card. Save the new hash so unchanged notes
// are not processed again.
//
// SD access follows scrobble's rules: read and write in chunks under spiLock;
// perform HTTP traffic only AFTER spiUnlock (network and SPI bus are separate).
// Runs only from Arduino loop()/the console (Core 1), never the audio task, so
// no mutex is needed.
// =============================================================================
#pragma once

#include <stddef.h>

namespace notes_ai {

// Number of notes changed since the last polish (excluding today's note).
// Scans the SD card; intended only for status/console use.
int pendingCount();

// Blocking run before automatic standby (manages Wi-Fi and uses RTC RAM for
// backoff after failures). No-op when there are no pending notes or no
// configuration. true = nothing to do or at least one note polished.
bool flushBeforeStandby();

// Like flushBeforeStandby(), but without backoff, for immediate console testing
// ("ollama flush"). logN receives a short status line.
bool flushNow(char* log, size_t logN);

// Connection test (console "ollama test"): turn Wi-Fi on, GET /api/tags, then
// turn Wi-Fi off. msg receives a status message.
bool ping(char* msg, size_t msgN);

}  // namespace notes_ai

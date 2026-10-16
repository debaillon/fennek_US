// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// podcast.h — Podcast-Abonnements: Feed-Verwaltung + Episoden-Sync.
//
// Background (CLAUDE.md): Wi-Fi and audio cannot run together on this hardware
// (the Wi-Fi task on Core 0 preempts the audio task, and both use the SPI bus).
// Sync therefore manages Wi-Fi itself (audio::stop + mesh suspend), streams
// episodes to the SD card (network I/O after spiUnlock, SD writes under
// spiLock), and then turns Wi-Fi off again, as in scrobble/notes_ai.
//
// Storage model (everything is on SD; silently skipped when no SD card is present):
//   /podcasts/feeds.txt        — Abos, eine Zeile "URL" oder "URL<TAB>Name".
//                                Editable through WebFM (like /meshcore/channels.txt).
//   /podcasts/<slug>/          — one directory per feed (slug from the URL).
//   /podcasts/<slug>/episode.* — the single retained (newest) episode.
//   /podcasts/<slug>/state.txt — guid/title/file for the locally available episode.
//
// Default policy: "keep only the latest episode" — sync downloads the first
// <item> in the feed and deletes every older audio file in the directory.
// =============================================================================
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace podcast {

constexpr int kMaxFeeds = 16;

struct Feed {
  char url[256];
  char name[64];    // Display name (from feeds.txt; otherwise the slug)
  char slug[40];    // Directory name under /podcasts
};

// Lokal vorliegende Episode eines Feeds (aus state.txt).
struct Local {
  char title[96];
  char guid[96];
  char file[128];   // Full SD path to the audio file ("" = none)
};

// Default feed used to populate feeds.txt on first startup.
constexpr const char* kDefaultFeedUrl  = "https://freakshow.fm/feed/mp3/";
constexpr const char* kDefaultFeedName = "Freakshow";

// Ensure /podcasts and feeds.txt exist (create the default feed if empty).
// SPI-lightweight; callable from main() after the SD card is mounted.
void begin();

// --- Feed management (reads/writes feeds.txt under spiLock) ------------------
int  readFeeds(Feed* out, int cap);
int  feedCount();
bool feed(int idx, Feed* out);
bool addFeed(const char* url, const char* name);   // Append to feeds.txt
bool removeFeed(int idx);

// Locally available episode (from <slug>/state.txt). false = none yet.
bool localEpisode(const Feed& f, Local* out);

// Progress callback for the UI (phase: short text; pct -1 = indeterminate).
typedef void (*Progress)(const char* phase, int pct);

// Manual sync (console/app): manages Wi-Fi itself and blocks. Downloads the
// latest episode for each feed if new and deletes older episodes. log receives
// a short status line. true = at least one new episode downloaded or nothing to do.
bool syncAll(char* log, size_t logN, Progress prog);

// Like syncAll, but for one feed (idx).
bool syncOne(int idx, char* log, size_t logN, Progress prog);

// Automatic sync BEFORE automatic standby (RTC-RAM back-off; only when
// settings::podcastAutoSync() is enabled). Syncs only ONE feed per standby
// (round-robin) to keep Wi-Fi use brief. No-op if feeds/settings are absent.
bool flushBeforeStandby();

}  // namespace podcast

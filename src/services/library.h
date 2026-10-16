// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// library.h — Track list (MP3 files in an SD directory).
//
// Built by the UI thread (SD scan) and read by the audio task
// (Next/Prev/EOF advance). Access is serialized with a mutex; the scan runs
// under the SPI mutex (shared bus).
// =============================================================================
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace library {

void begin();

// --- Build the library --------------------------------------------------------
// Normal boot path: loadCache() loads /.fennek/tracks.bin (fast); without a
// cache, startScan() + scanStep() run in chunks from the appmgr loop (readdir,
// keeping the UI responsive). On completion, results are published, the ID3
// cache is applied, and the track cache is written. Automatically falls back
// to "/" if nothing is found under the starting directory.
bool loadCache();
void startScan(const char* dir);
bool scanning();
int  scanFound();                 // Tracks found so far (only while a scan is running)
void scanStep(int maxEntries);

// Blocking full scan (self-tests). Returns the track count.
int scan(const char* dir);

int count();

// Copy the display name (filename) of the i-th track to out.
bool name(int i, char* out, size_t outLen);

// Copy the full SD path of the i-th track to out.
bool path(int i, char* out, size_t outLen);

// Artist/album of the i-th track (ID3 tag or directory fallback, UTF-8).
bool trackArtist(int i, char* out, size_t outLen);
bool trackAlbum(int i, char* out, size_t outLen);

// Flat index for an SD path (-1 = not in the library).
int indexOfPath(const char* p);

// --- ID3 tag scan (incremental) ----------------------------------------------
// scan() first applies the cache (/.fennek/id3.bin); files without a cache hit
// are then parsed in chunks through tagScanStep() (called from the appmgr
// loop) to keep the UI responsive. After the final step, indices are resorted
// and the cache is rewritten.
bool tagScanPending();
void tagScanStep(int maxFiles);
void tagScanProgress(int* done, int* total);

// --- Hierarchy queries (artist/album/title) ---------------------------------
// Artists/albums are derived from the directory structure:
//   <root>/<artist>/<album>/<title>.mp3
// The flat track list is sorted by (artist, album, name); artist and album
// runs (contiguous ranges) are precomputed from it.

// Artists (alphabetical).
int  artistCount();
void artistName(int artist, char* out, size_t outLen);

// Albums by an artist -> returns global album run IDs.
int  artistAlbumCount(int artist);
int  artistAlbumRun(int artist, int j);

// All albums (sorted by album name) -> global album run IDs.
int  albumCount();
int  albumRunByName(int j);

// Album run (a specific album).
void albumLabel(int run, bool withArtist, char* out, size_t outLen);
int  albumTrackCount(int run);
void albumTrackName(int run, int k, char* out, size_t outLen);
int  albumTrackFlatIndex(int run, int k);     // -> index for audio::playIndex

// All titles (sorted by title).
int  titleCount();
void titleName(int i, char* out, size_t outLen);
int  titleFlatIndex(int i);                   // -> index for audio::playIndex

// --- Actual .m3u/.m3u8 playlists --------------------------------------------
// Playlist files found during the scan. When opened, the m3u is parsed and
// each entry (relative to the playlist directory or absolute) is resolved to a
// library track (path match, then filename match).
int  playlistCount();
void playlistName(int i, char* out, size_t outLen);   // Dateiname der m3u
int  playlistOpen(int i);                              // parst, cached; #aufgelöste Tracks
void playlistTrackName(int k, char* out, size_t outLen);
int  playlistTrackFlatIndex(int k);                   // -> Index für audio

// Collect flat indices (in order) for a playback queue.
// scope: artist/album/title/playlist functions return indices individually;
// this helper writes all flat indices for an album run to out.
int  albumTrackFlatList(int run, int* out, int maxN);

}  // namespace library

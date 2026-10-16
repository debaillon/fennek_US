// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// id3.h — minimal ID3 tag reader (title/artist/album).
//
// Supports ID3v2.2/2.3/2.4 (TIT2/TPE1/TALB or TT2/TP1/TAL frames) with
// Latin-1, UTF-16, and UTF-8 encodings, plus ID3v1 as a fallback. Results are
// returned as UTF-8 (the display converts them via gui::toCp437).
//
// Frame payloads are skipped with seek rather than read, so files with large
// cover-art frames (APIC) are still scanned quickly.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace id3 {

struct Tags {
  char title[64];
  char artist[48];
  char album[48];
  uint32_t fsize;   // File size (0 = file could not be opened). The library's
                    // readdir scan has no sizes, so they are captured when
                    // the file is opened here as part of normal processing.
};

// Read tags from an audio file. Acquires spiLock() internally.
// Returns true if at least one field was found (empty fields = "").
bool read(const char* path, Tags& out);

}  // namespace id3

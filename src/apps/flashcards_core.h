// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// flashcards_core.h — flashcard logic (Leitner) and line parser, independent of
// Arduino (host-testable). SD I/O and PSRAM card storage are in
// apps/flashcards_app.cpp; this file contains only pure, testable logic.
//
// Deck format (one card per line):
//   Front<TAB>Back (preferred)
//   Front | Back (pipe as fallback)
// Lines beginning with '#' and blank lines are comments/separators.
//
// Learning progress uses Leitner boxes 0..4. A card is due when its box
// interval has elapsed since it was last seen (the device has a deep-sleep-
// persistent system clock; see timesync). A correct answer advances one box;
// an incorrect answer returns it to box 0.
// =============================================================================
#pragma once

#include <stdint.h>
#include <string.h>

namespace flashcards {

constexpr int kBoxCount = 5;

// A study session includes at most this many distinct cards. Incorrect answers
// are repeated during the session, but do not count as new cards.
constexpr int kSessionLimit = 10;

// --- Selection: shuffle + "learned cards less often" --------------------------
// Both functions operate on an index list (idx[0..n)), not on the cards
// themselves, keeping the core independent of the app's card structure.

// In-place Fisher-Yates shuffle. `rnd(m)` returns a number in [0, m).
inline void shuffle(int* idx, int n, uint32_t (*rnd)(uint32_t)) {
  for (int i = n - 1; i > 0; i--) {
    int j = (int)rnd((uint32_t)(i + 1));
    int t = idx[i]; idx[i] = idx[j]; idx[j] = t;
  }
}

// Stable ascending sort by Leitner box (lower boxes first = less-learned cards
// are preferred). Insertion sort preserves the shuffled order within each box,
// so cards of the same priority remain shuffled.
// `boxOf(i)` returns the box for card index i (a lambda/functor in the app).
template <typename BoxFn>
inline void stableSortByBox(int* idx, int n, BoxFn boxOf) {
  for (int i = 1; i < n; i++) {
    int v = idx[i];
    uint8_t bv = boxOf(v);
    int j = i - 1;
    while (j >= 0 && boxOf(idx[j]) > bv) { idx[j + 1] = idx[j]; j--; }
    idx[j + 1] = v;
  }
}

// Seconds until a card in box `box` becomes due again.
inline uint32_t boxInterval(uint8_t box) {
  constexpr uint32_t kDay = 86400u;
  switch (box) {
    case 0:  return 0;          // Immediately (same study session)
    case 1:  return 1 * kDay;
    case 2:  return 3 * kDay;
    case 3:  return 7 * kDay;
    default: return 16 * kDay;  // Box 4
  }
}

// Box transition after an answer is graded.
inline uint8_t nextBox(uint8_t box, bool correct) {
  if (!correct) return 0;
  if (box + 1 >= kBoxCount) return (uint8_t)(kBoxCount - 1);
  return (uint8_t)(box + 1);
}

// Check whether a card is due. New cards (lastSeen==0) are always due.
inline bool isDue(uint8_t box, uint32_t lastSeen, uint32_t nowEpoch) {
  if (lastSeen == 0) return true;
  return nowEpoch >= lastSeen + boxInterval(box);
}

// --- Zeilen-Parser -----------------------------------------------------------
struct Card {
  char front[96];
  char back[96];
};

namespace detail {
inline const char* skipWs(const char* s) {
  while (*s == ' ' || *s == '\t') s++;
  return s;
}
// Copy the trimmed range [s, end) to dst (at most cap bytes, including NUL).
inline void copyTrim(char* dst, size_t cap, const char* s, const char* end) {
  while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
                     end[-1] == '\r' || end[-1] == '\n'))
    end--;
  s = skipWs(s);
  size_t n = 0;
  while (s < end && n + 1 < cap) dst[n++] = *s++;
  dst[n] = 0;
}
}  // namespace detail

// Parse one line. Return false for comments, blank lines, missing separators,
// or an empty side.
inline bool parseLine(const char* line, Card* out) {
  const char* p = detail::skipWs(line);
  if (*p == 0 || *p == '#' || *p == '\r' || *p == '\n') return false;

  // Find the line ending so a '|' from the next line is not included.
  const char* eol = line;
  while (*eol && *eol != '\n') eol++;

  const char* sep = nullptr;
  for (const char* q = line; q < eol; q++)
    if (*q == '\t') { sep = q; break; }
  if (!sep)
    for (const char* q = line; q < eol; q++)
      if (*q == '|') { sep = q; break; }
  if (!sep) return false;

  detail::copyTrim(out->front, sizeof(out->front), line, sep);
  detail::copyTrim(out->back, sizeof(out->back), sep + 1, eol);
  return out->front[0] != 0 && out->back[0] != 0;
}

// Stable card key derived from the front (survives deck reordering; uses the
// same polynomial variant as settings::crc32).
inline uint32_t cardKey(const char* front) {
  uint32_t crc = 0xFFFFFFFFu;
  for (const char* s = front; *s; ++s) {
    crc ^= (uint8_t)*s;
    for (int i = 0; i < 8; i++)
      crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
  }
  return ~crc;
}

}  // namespace flashcards

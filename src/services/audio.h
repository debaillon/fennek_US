// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// audio.h — Playback (ESP32-audioI2S → PCM5102A), path-based queue.
//
// Anti-stutter architecture (unchanged):
//   - Decoding runs in a SEPARATE FreeRTOS task on Core 0; the UI runs on
//     Core 1, so there is no CPU contention.
//   - audio.loop() runs under the SPI mutex; during E-Ink BUSY periods,
//     display.cpp releases the bus and the 256-KB PSRAM read-ahead buffer helps.
//   - All library audio.* calls happen ONLY in the audio task; the UI sends
//     commands through a queue.
//
// Multi-app support:
//   - The playback queue contains SD PATHS (not library indices), so the
//     audiobook app can use the pipeline without depending on library.cpp.
//   - Owner token (Music/Book): starting the other owner stops current
//     playback; the displaced owner can detect this through status().owner.
//   - Shuffle/repeat, relative seek (CBR MP3), start offset, sleep timer.
// =============================================================================
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace audio {

enum class Owner  : uint8_t { None, Music, Book, Podcast };
enum class Repeat : uint8_t { Off, All, One };

struct Status {
  bool     playing;
  bool     paused;
  int      pos;        // Position in the playback queue (-1 = none)
  int      queueLen;
  uint32_t posSec;
  uint32_t durSec;
  uint8_t  volume;     // 0..AUDIO_VOL_MAX
  Owner    owner;
  bool     shuffle;
  Repeat   repeat;
  uint16_t sleepMin;   // Remaining sleep-timer minutes (rounded up), 0 = off
};

void begin();

// --- Set queue (staging, then atomic commit) ---------------------------------
// queueBegin/queueAdd run on the UI thread (no SD access); queueCommit passes
// the queue to the audio task and starts at startPos (+startSec).
void queueBegin(Owner owner);
bool queueAdd(const char* path);            // false if full
void queueCommit(int startPos, uint32_t startSec = 0);

// Path of the currently playing track ("" if none).
void currentPath(char* out, size_t n);

void togglePause();
void next();
void prev();
void stop();

// I2S0 handoff for PDM microphone recording (services/mic): beginMic() releases
// I2S0 (playback stops), and endMic() takes it back. The engine ignores play
// commands while recording.
bool beginMic();
void endMic();
void seekRel(int sec);                      // ±seconds (reliable with CBR MP3)
void setVolume(uint8_t v);
void volumeUp();
void volumeDown();
void setShuffle(bool on);                   // Current track stays current
void setRepeat(Repeat r);
void setSleepTimer(uint16_t minutes);       // 0 = off; stops playback when elapsed

Status status();

}  // namespace audio

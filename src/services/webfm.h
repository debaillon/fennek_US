// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// webfm.h — Web file management: Wi-Fi (station) + HTTP server for the SD card.
//
// Connects to the home Wi-Fi network (SSID/password from settings::wifiSsid/
// wifiPass, entered through the serial console: "wifi ssid/pass ...") and serves
// a small HTML page (webfm_html.h) plus a JSON API to list, upload, download,
// and delete SD card files.
//
// Architektur-Regeln (CLAUDE.md):
//  - Audio stops and the mesh pump pauses at startup: the Wi-Fi task (Core 0,
//    high priority) would preempt the audio task, and file transfers compete
//    for the SPI bus.
//  - HTTP handlers perform SD access in chunks under spiLock(); network I/O
//    always happens AFTER spiUnlock() so E-Ink/UI is never starved by a slow
//    TCP send.
//  - Call poll() from the UI thread (tick() in the files app or console), never
//    from a draw context (handlers acquire spiLock() themselves).
// =============================================================================
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace webfm {

enum class State : uint8_t {
  OFF,         // Wi-Fi off
  CONNECTING,  // WiFi.begin() in progress (15 s timeout)
  RUNNING,     // Connected; server accepts requests
  FAILED,      // Connection failed (Wi-Fi off again)
};

// Start Wi-Fi and server. false = no SSID configured. Stops audio and pauses
// the mesh pump. Idempotent while CONNECTING/RUNNING.
bool start();

// Stop server and Wi-Fi; the mesh pump resumes (audio remains stopped).
void stop();

// State machine + server.handleClient(); call once per loop iteration.
void poll();

State state();
void  ipStr(char* out, size_t n);       // "192.168.x.y" (RUNNING only, else "")
void  ssid(char* out, size_t n);        // Configured SSID
uint32_t requestCount();                // HTTP requests served since start()

}  // namespace webfm

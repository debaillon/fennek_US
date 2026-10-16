// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// ota.h — OTA firmware update (download from URL).
//
// Source: the GitHub Releases API for danst0/fennek (used directly as the
// manifest) or a self-hosted JSON manifest. The update is written to the
// inactive OTA slot (app0/app1) in internal QSPI flash; the shared HSPI bus
// (SD/E-Ink/LoRa) is NOT affected. Audio is stopped and the Mesh pump is
// suspended during Wi-Fi operation (WebFM rule).
//
// check()/apply() require a Wi-Fi connection (e.g. webfm RUNNING). The
// console* helpers bring up Wi-Fi themselves (headless, without a display).
// =============================================================================
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace ota {

struct CheckResult {
  bool ok          = false;   // Manifest downloaded and parsed
  bool updateAvail = false;   // latest != current
  char current[16] = "";      // FENNEK_VERSION
  char latest[16]  = "";      // tag_name from the release
  char url[200]    = "";      // browser_download_url for firmware.bin
  char err[80]     = "";      // Error text (valid when !ok)
};

// Read the manifest at manifestUrl (GitHub API: tag_name + first .bin asset;
// fallback: "version"/"url"). Wi-Fi must be connected.
CheckResult check(const char* manifestUrl);

// Download firmware from downloadUrl (resolve HTTP 302 redirects) and flash it
// to the inactive OTA slot. true = success; the caller reboots afterward
// (apply intentionally does NOT reboot). Wi-Fi must be connected.
bool apply(const char* downloadUrl, char* errOut, size_t errCap);

// Headless console check: bring up Wi-Fi (stop audio and suspend Mesh), check
// status, print it to Serial, then turn Wi-Fi off. Does not flash firmware.
void consoleCheck();

// Headless console update: check and, if needed, flash and reboot. force = flash
// without comparing versions (reflash/downgrade).
void consoleUpdate(bool force);

// --- On-device path (Settings app, blocking with UI progress) ----------------
// UI progress callback: phase = short text (e.g. "Connecting to Wi-Fi"),
// pct = 0..100 or <0 for indeterminate (no percentage). Called synchronously
// from deviceCheck()/deviceApply(); the handler may update E-Ink (audio and
// Mesh are idle during Wi-Fi operation, leaving the SPI bus free). Both
// functions block and run only from loop() context (Core 1), never the audio
// task, just like the console path.
typedef void (*ProgressFn)(const char* phase, int pct);

// Bring up Wi-Fi, check the manifest, then turn Wi-Fi off. Requires Wi-Fi
// credentials in NVS and fails if WebFM is already running. The optional
// progress callback receives phases such as "Connecting to Wi-Fi" and
// "Checking version".
CheckResult deviceCheck(ProgressFn progress);

// Bring up Wi-Fi and flash firmware from downloadUrl (progress is reported via
// progress, 0..100%). On success, reboot (does NOT return); on failure, turn
// Wi-Fi off and return false with the reason in errOut.
bool deviceApply(const char* downloadUrl, ProgressFn progress, char* errOut, size_t errCap);

}  // namespace ota

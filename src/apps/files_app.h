// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// files_app.h — Files app: manage SD card files over Wi-Fi.
//
// Thin UI wrapper around services/webfm: Wi-Fi starts only on explicit user
// action (Enter/tap, not on entry; Wi-Fi uses power) and stops when leaving the
// app. While the server is running, audio is stopped and the Mesh pump paused
// due to SPI/Core contention (see webfm.h).
// =============================================================================
#pragma once

#include "core/appmgr.h"

namespace files_app {

App* get();

}  // namespace files_app

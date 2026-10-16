// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// launcher.h — Homescreen mit App-Kacheln.
//
// The first app registered with appmgr; tapping a tile launches its app.
// Tiles without an app (nullptr) are placeholders ("coming soon").
// =============================================================================
#pragma once

#include "core/appmgr.h"
#include "core/i18n.h"

namespace launcher {

App* get();

// Kachel idx (0..5) belegen. app == nullptr -> deaktivierter Platzhalter.
// Store the label as a string ID for resolution when the tile is drawn.
void setTile(int idx, i18n::Str label, App* app);

}  // namespace launcher

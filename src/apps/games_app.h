// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// games_app.h — "Games": menu wrapper for 2048, Minesweeper, chess, and Tic-Tac-Toe.
//
// One registered app with internal screens (like music_app/mesh_app);
// game modules (game2048.*, mines.*, chess.*, ttt.*) provide enter/
// handleInput/draw are dispatched from here. Game state lives in static
// variables and survives app switches; chess is also persisted to NVS.
// =============================================================================
#pragma once

#include "core/appmgr.h"

namespace games_app {

App* get();

// Return to the games menu (called by games when Backspace is pressed).
void showMenu();

}  // namespace games_app

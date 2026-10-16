// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// gui.h — shared drawing helpers for all apps.
//
// All text output uses gui::print()/drawButton()/drawRowText(), which convert
// UTF-8 to CP437. This requires display::begin() to enable cp437(true).
// =============================================================================
#pragma once

#include <Adafruit_GFX.h>
#include <stddef.h>
#include <stdint.h>

namespace gui {

struct Rect {
  int x, y, w, h;
  bool hit(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// UTF-8 to CP437. Unsupported characters become '?'; typographic characters
// (em dashes, quotation marks, ellipses) are converted to ASCII equivalents.
void toCp437(const char* utf8, char* out, size_t outLen);

// Unicode code point to CP437 byte (0 = not available in the font).
uint8_t cp437For(uint32_t cp);

// Print UTF-8 text at the current cursor position.
void print(Adafruit_GFX& g, const char* utf8);

// Print UTF-8 text at a position (sets the cursor and text size).
void printAt(Adafruit_GFX& g, int x, int y, const char* utf8, uint8_t size = 2);

// Pixel width and height of UTF-8 text at the current text size.
void textBounds(Adafruit_GFX& g, const char* utf8, uint16_t* w, uint16_t* h);

// Labeled button with rounded corners; emphasize = double border.
void drawButton(Adafruit_GFX& g, const Rect& r, const char* utf8, bool emphasize);

// Full-width list row with a divider; invert = black background.
void drawRowText(Adafruit_GFX& g, int y, int rowH, const char* utf8, bool invert);

}  // namespace gui

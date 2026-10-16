// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// chess_core.h — Schach-Spiellogik + KI, Arduino-frei (host-testbar).
//
// Full rules: castling, en passant, promotion, check/checkmate/stalemate, and
// the 50-move rule. The UI checks repeated positions via hashPos().
// Zuggenerator ist perft-verifiziert (tools/host_test_games.cpp).
//
// KI: Negamax mit Alpha-Beta, Capture-Quiescence und iterativer Vertiefung;
// Evaluation = material + piece-square tables. Pure CPU work with no hardware
// access; runs on the device in its own task.
// =============================================================================
#pragma once

#include <stdint.h>

namespace chess {

// Pieces: positive value = White, negative = Black; magnitude = piece type.
enum : int8_t { EMPTY = 0, PAWN = 1, KNIGHT = 2, BISHOP = 3, ROOK = 4, QUEEN = 5, KING = 6 };

// Rochade-Rechte als Bits.
enum : uint8_t { CASTLE_WK = 1, CASTLE_WQ = 2, CASTLE_BK = 4, CASTLE_BQ = 8 };

// Feld-Index: a1 = 0 … h8 = 63 (rank*8 + file).
struct Pos {
  int8_t  board[64];
  int8_t  stm;        // Side to move: 1 = White, -1 = Black
  uint8_t castle;
  int8_t  ep;         // En-passant-Zielfeld, -1 = keins
  uint8_t halfmove;   // Half-moves since a pawn move or capture (50-move rule)
};

struct Move {
  uint8_t from, to;
  int8_t  promo;      // 0 oder Zieltyp (KNIGHT..QUEEN) bei Umwandlung
};

constexpr int MAX_MOVES = 218;

void startPos(Pos& p);

// Is `sq` attacked by side `by` (1/-1)?
bool attacked(const Pos& p, int sq, int8_t by);
bool inCheck(const Pos& p, int8_t side);

// Legal moves only; returns the count (out needs MAX_MOVES slots).
int genMoves(const Pos& p, Move* out);

void makeMove(Pos& p, const Move& m);

enum Result : uint8_t { ONGOING, MATE, STALEMATE, DRAW50 };
Result result(const Pos& p);   // MATE/STALEMATE betrifft die Seite am Zug

// FNV-1a over the board + side to move + castling + en passant
// (for repeated-position detection).
uint32_t hashPos(const Pos& p);

// Node counter for move-generator tests.
uint64_t perft(const Pos& p, int depth);

// Besten Zug suchen: iterative Vertiefung bis maxDepth; bricht weich ab,
// once nodeLimit is exceeded (the last completed depth still counts).
// false = no legal move (checkmate/stalemate). score is optional and from the
// side-to-move perspective.
bool search(const Pos& p, int maxDepth, uint32_t nodeLimit, Move* best,
            int* score = nullptr, uint32_t* nodesOut = nullptr);

// --- Trainer-Analyse ---------------------------------------------------------
// Classify a player's move relative to the engine's best move.
enum MoveClass : uint8_t { MC_BEST, MC_GOOD, MC_INACCURACY, MC_MISTAKE, MC_BLUNDER };

struct MoveEval {
  MoveClass cls;
  int  lossCp;        // Zentibauern-Verlust ggü. bestem Zug (>= 0)
  int  scoreAfter;    // Bewertung aus Schülersicht nach dem Zug (cp; Matt ~ ±100000)
  Move best;          // Engine-Empfehlung in der Ausgangsstellung
  bool playedIsBest;  // Schülerzug == Empfehlung
  bool isCapture;     // Schülerzug schlug etwas
  bool givesCheck;    // Schülerzug gibt Schach
  bool hangsPiece;    // gezogene Figur steht danach ungedeckt im Angriff
  bool missedCapture; // Empfehlung war ein Schlag, den der Schüler ausließ
  bool getsMated;     // Zug führt in absehbares Matt gegen den Schüler
  bool missedMate;    // Empfehlung war ein Matt, Schüler spielte anders
  Move reply;         // Antwortzug des Trainers (Folgestellung), gültig bei hasReply
  bool hasReply;      // Folgestellung hat legale Züge (Partie läuft weiter)
};

// Schülerzug `played` in Stellung `before` (Schüler am Zug) bewerten. Führt zwei
// Suchen (bester Zug in `before` + Bewertung/Antwort in der Folgestellung).
// false nur, wenn `before` keinen legalen Zug hat (dürfte nach einem realen Zug
// nie vorkommen). Reine CPU-Arbeit — im Schach-KI-Task aufrufen.
bool analyzeMove(const Pos& before, const Move& played, int maxDepth,
                 uint32_t nodeLimit, MoveEval* out);

}  // namespace chess

// =============================================================================
// host_test_games.cpp — host tests for the game cores (not part of the
// firmware build; intentionally kept outside src/).
//
//   g++ -std=c++17 -O2 -I src tools/host_test_games.cpp src/apps/chess_core.cpp \
//       -o /tmp/games_test && /tmp/games_test
//
// Precedent: the textdoc paginator was also host-tested (see CLAUDE.md).
// =============================================================================
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "apps/game2048_core.h"
#include "apps/mines_core.h"
#include "apps/ttt_core.h"
#include "apps/chess_core.h"
#include "apps/sudoku_core.h"   // header-only; no additional .cpp in the g++ command

// --- Deterministic random numbers (LCG) --------------------------------------
static uint32_t s_seed = 12345;
static uint32_t testRnd(uint32_t n) {
  s_seed = s_seed * 1664525u + 1013904223u;
  return (s_seed >> 16) % n;
}
static uint32_t zeroRnd(uint32_t) { return 0; }

static int s_checks = 0;
#define CHECK(cond) do { \
  if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } \
  s_checks++; \
} while (0)

// =============================================================================
// 2048
// =============================================================================
static void setLine(game2048::Board& b, uint8_t a, uint8_t c, uint8_t d, uint8_t e) {
  for (int i = 0; i < 16; i++) b.cell[i] = 0;
  b.cell[0] = a; b.cell[1] = c; b.cell[2] = d; b.cell[3] = e;
}

static int test2048() {
  using namespace game2048;
  Board b;
  uint32_t gain;

  // [2,2,2,2] -> [4,4] (no triple merge), score 8
  setLine(b, 1, 1, 1, 1);
  gain = 0;
  CHECK(slide(b, LEFT, &gain));
  CHECK(b.cell[0] == 2 && b.cell[1] == 2 && b.cell[2] == 0 && b.cell[3] == 0);
  CHECK(gain == 8);

  // [4,2,2,_] -> [4,4]: merge the trailing twos; the fours remain separate
  setLine(b, 2, 1, 1, 0);
  gain = 0;
  CHECK(slide(b, LEFT, &gain));
  CHECK(b.cell[0] == 2 && b.cell[1] == 2 && b.cell[2] == 0);
  CHECK(gain == 4);

  // [2,2,4,_] -> [4,4], NOT [8]: a newly merged tile cannot merge again
  setLine(b, 1, 1, 2, 0);
  gain = 0;
  CHECK(slide(b, LEFT, &gain));
  CHECK(b.cell[0] == 2 && b.cell[1] == 2 && b.cell[2] == 0);
  CHECK(gain == 4);

  // RIGHT spiegelt: [2,2,_,_] -> [_,_,_,4]
  setLine(b, 1, 1, 0, 0);
  CHECK(slide(b, RIGHT, nullptr));
  CHECK(b.cell[3] == 2 && b.cell[0] == 0 && b.cell[1] == 0 && b.cell[2] == 0);

  // UP: column 0 contains [2,2] at the top, so it merges upward
  for (int i = 0; i < 16; i++) b.cell[i] = 0;
  b.cell[0] = 1; b.cell[4] = 1;
  CHECK(slide(b, UP, nullptr));
  CHECK(b.cell[0] == 2 && b.cell[4] == 0);

  // No moves are possible on a full checkerboard pattern.
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      b.cell[r * 4 + c] = (uint8_t)(((r + c) % 2) ? 1 : 2);
  CHECK(!canMove(b));
  CHECK(!slide(b, LEFT, nullptr) && !slide(b, RIGHT, nullptr));
  CHECK(!slide(b, UP, nullptr) && !slide(b, DOWN, nullptr));

  // A move that changes nothing does not count: [2,_,_,_] LEFT
  setLine(b, 1, 0, 0, 0);
  CHECK(!slide(b, LEFT, nullptr));

  // Spawn uses only empty cells (15 occupied, one empty).
  for (int i = 0; i < 16; i++) b.cell[i] = 1;
  b.cell[9] = 0;
  spawn(b, zeroRnd);
  CHECK(b.cell[9] == 2);   // zeroRnd: rnd(10)==0 -> Vierer (Exponent 2)
  CHECK(countEmpty(b) == 0);

  // hasTile / reset
  CHECK(hasTile(b, 1) && !hasTile(b, 3));
  reset(b, testRnd);
  CHECK(countEmpty(b) == 14);

  printf("2048: ok\n");
  return 0;
}

// =============================================================================
// Minesweeper
// =============================================================================
static int testMines() {
  using namespace mines;
  Field f;

  // firstReveal: never place mines on the first cell or its neighbors; exactly
  // 15 mines and correct adjacent-mine counts.
  for (int trial = 0; trial < 20; trial++) {
    init(f, 10, 11, 15);
    int start = (trial * 37) % 110;
    firstReveal(f, start, testRnd);
    int mineCount = 0;
    for (int i = 0; i < 110; i++)
      if (f.cell[i] & MINE) mineCount++;
    CHECK(mineCount == 15);
    CHECK(!(f.cell[start] & MINE));
    for (int k = 0; k < 8; k++) {
      int nb;
      if (neighbor(f, start, k, &nb)) CHECK(!(f.cell[nb] & MINE));
    }
    for (int i = 0; i < 110; i++) {
      if (f.cell[i] & MINE) continue;
      int cnt = 0;
      for (int k = 0; k < 8; k++) {
        int nb;
        if (neighbor(f, i, k, &nb) && (f.cell[nb] & MINE)) cnt++;
      }
      CHECK((f.cell[i] & NUM_MASK) == cnt);
    }
    CHECK(f.state == RUNNING || f.state == WON);
    // First cell is a zero (neighbors are safe) -> it and all neighbors are open.
    int nbCount = 0;
    for (int k = 0; k < 8; k++) {
      int nb;
      if (neighbor(f, start, k, &nb)) {
        nbCount++;
        CHECK(f.cell[nb] & OPEN);
      }
    }
    CHECK((f.cell[start] & OPEN) && f.revealed >= 1 + nbCount);
  }

  // Flood fill on a fixed 5x5 board: one mine at corner 0; revealing cell 24
  // opens every other cell, so the game is won immediately.
  init(f, 5, 5, 1);
  f.placed = true;
  f.cell[0] |= MINE;
  for (int i = 0; i < 25; i++) {
    if (f.cell[i] & MINE) continue;
    int cnt = 0;
    for (int k = 0; k < 8; k++) {
      int nb;
      if (neighbor(f, i, k, &nb) && (f.cell[nb] & MINE)) cnt++;
    }
    f.cell[i] |= (uint8_t)cnt;
  }
  CHECK(reveal(f, 24));
  CHECK(f.state == WON);
  CHECK(f.revealed == 24);
  CHECK(!(f.cell[0] & OPEN));

  // Mine erwischt -> LOST
  init(f, 5, 5, 1);
  f.placed = true;
  f.cell[12] |= MINE;
  CHECK(!reveal(f, 12));
  CHECK(f.state == LOST);

  // A flag blocks revealing and flood fill.
  init(f, 5, 5, 1);
  f.placed = true;
  f.cell[0] |= MINE;
  for (int i = 1; i < 25; i++) {
    int cnt = 0;
    for (int k = 0; k < 8; k++) {
      int nb;
      if (neighbor(f, i, k, &nb) && (f.cell[nb] & MINE)) cnt++;
    }
    f.cell[i] |= (uint8_t)cnt;
  }
  toggleFlag(f, 24);
  CHECK(f.flags == 1);
  CHECK(reveal(f, 24));            // ignored (flagged)
  CHECK(f.revealed == 0);
  toggleFlag(f, 24);
  CHECK(f.flags == 0);
  toggleFlag(f, 7);                // flag a cell in the zero region
  CHECK(reveal(f, 24));
  CHECK(f.state == RUNNING);       // cell 7 remains closed, so not won yet
  CHECK(!(f.cell[7] & OPEN));

  printf("Minesweeper: ok\n");
  return 0;
}

// =============================================================================
// Tic-Tac-Toe: Minimax darf nie verlieren (kompletter Spielbaum)
// =============================================================================
static int s_tttGames = 0;

// Opponent (human) tries every move; the AI responds with bestMove.
static bool tttNeverLoses(ttt::Board b, uint8_t ai, uint8_t turn) {
  int w = ttt::winner(b);
  if (w) {
    s_tttGames++;
    return w != (3 - ai);   // any result except an opponent win is acceptable
  }
  if (turn == ai) {
    int m = ttt::bestMove(b, ai);
    if (m < 0) return true;
    b.c[m] = ai;
    return tttNeverLoses(b, ai, (uint8_t)(3 - ai));
  }
  for (int i = 0; i < 9; i++) {
    if (b.c[i]) continue;
    ttt::Board t = b;
    t.c[i] = turn;
    if (!tttNeverLoses(t, ai, ai)) return false;
  }
  return true;
}

static int testTtt() {
  using namespace ttt;
  Board b;
  reset(b);

  // Win-Checks
  b.c[0] = b.c[1] = b.c[2] = 1;
  CHECK(winner(b) == 1);
  reset(b);
  b.c[0] = b.c[4] = b.c[8] = 2;
  CHECK(winner(b) == 2);
  reset(b);
  uint8_t draw[9] = {1,2,1, 1,2,2, 2,1,1};
  memcpy(b.c, draw, 9);
  CHECK(winner(b) == 3);

  // KI blockt unmittelbare Drohung
  reset(b);
  b.c[0] = b.c[1] = 1;            // X droht 0-1-2
  CHECK(bestMove(b, 2) == 2);

  // The AI never loses, whether moving first or second.
  reset(b);
  CHECK(tttNeverLoses(b, 2, 1));
  CHECK(tttNeverLoses(b, 1, 1));
  printf("TTT: ok (%d terminal positions)\n", s_tttGames);
  return 0;
}

// =============================================================================
// Chess: perft + checkmate/stalemate + search
// =============================================================================

// Minimal FEN parser for tests only (the core does not need one).
static void fromFen(chess::Pos& p, const char* fen) {
  for (int i = 0; i < 64; i++) p.board[i] = 0;
  int r = 7, f = 0;
  const char* s = fen;
  for (; *s && *s != ' '; s++) {
    char c = *s;
    if (c == '/') { r--; f = 0; continue; }
    if (c >= '1' && c <= '8') { f += c - '0'; continue; }
    int8_t t = 0;
    switch (tolower(c)) {
      case 'p': t = chess::PAWN; break;
      case 'n': t = chess::KNIGHT; break;
      case 'b': t = chess::BISHOP; break;
      case 'r': t = chess::ROOK; break;
      case 'q': t = chess::QUEEN; break;
      case 'k': t = chess::KING; break;
    }
    p.board[r * 8 + f] = isupper((unsigned char)c) ? t : (int8_t)-t;
    f++;
  }
  s++;
  p.stm = (*s == 'w') ? 1 : -1;
  s += 2;
  p.castle = 0;
  if (*s == '-') {
    s++;
  } else {
    for (; *s && *s != ' '; s++) {
      switch (*s) {
        case 'K': p.castle |= chess::CASTLE_WK; break;
        case 'Q': p.castle |= chess::CASTLE_WQ; break;
        case 'k': p.castle |= chess::CASTLE_BK; break;
        case 'q': p.castle |= chess::CASTLE_BQ; break;
      }
    }
  }
  s++;
  p.ep = -1;
  if (*s && *s != '-') p.ep = (int8_t)((s[1] - '1') * 8 + (s[0] - 'a'));
  p.halfmove = 0;
}

static int testChess() {
  using namespace chess;
  Pos p;

  // Perft: starting position (reference values from chessprogramming.org).
  startPos(p);
  CHECK(perft(p, 1) == 20);
  CHECK(perft(p, 2) == 400);
  CHECK(perft(p, 3) == 8902);
  CHECK(perft(p, 4) == 197281);

  // Kiwipete: deckt Rochade, en passant, Umwandlung, Fesselung ab
  fromFen(p, "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -");
  CHECK(perft(p, 1) == 48);
  CHECK(perft(p, 2) == 2039);
  CHECK(perft(p, 3) == 97862);

  // Position 3 (en-passant-lastig)
  fromFen(p, "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -");
  CHECK(perft(p, 1) == 14);
  CHECK(perft(p, 2) == 191);
  CHECK(perft(p, 3) == 2812);
  CHECK(perft(p, 4) == 43238);

  // Position 4 (Umwandlungs-lastig)
  fromFen(p, "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq -");
  CHECK(perft(p, 1) == 6);
  CHECK(perft(p, 2) == 264);
  CHECK(perft(p, 3) == 9467);

  // Checkmate detected: back-rank mate on the board.
  fromFen(p, "R5k1/5ppp/8/8/8/8/8/6K1 b - -");
  CHECK(result(p) == MATE);

  // Patt erkannt
  fromFen(p, "7k/5Q2/6K1/8/8/8/8/8 b - -");
  CHECK(!inCheck(p, -1));
  CHECK(result(p) == STALEMATE);

  // Suche findet Matt in 1 (Ta1-a8)
  fromFen(p, "6k1/5ppp/8/8/8/8/8/R5K1 w - -");
  Move best;
  CHECK(search(p, 2, 0, &best));
  CHECK(best.from == 0 && best.to == 56);
  Pos after = p;
  makeMove(after, best);
  CHECK(result(after) == MATE);

  // Search returns a legal move from the starting position (depth 3).
  startPos(p);
  uint32_t nodes = 0;
  CHECK(search(p, 3, 0, &best, nullptr, &nodes));
  Move legal[MAX_MOVES];
  int n = genMoves(p, legal);
  bool found = false;
  for (int i = 0; i < n; i++)
    if (legal[i].from == best.from && legal[i].to == best.to) found = true;
  CHECK(found);
  printf("Chess: ok (starting-position depth-3 search: %u nodes)\n", nodes);

  // Promotion: a pawn on the 7th rank becomes a queen and gives check.
  fromFen(p, "8/P6k/8/8/8/8/8/K7 w - -");
  n = genMoves(p, legal);
  int promos = 0;
  for (int i = 0; i < n; i++)
    if (legal[i].promo) promos++;
  CHECK(promos == 4);

  // 50-move rule.
  startPos(p);
  p.halfmove = 100;
  CHECK(result(p) == DRAW50);

  // hashPos unterscheidet Zugrecht
  startPos(p);
  uint32_t h1 = hashPos(p);
  p.stm = -1;
  CHECK(hashPos(p) != h1);

  // --- Trainer-Analyse ---
  // Hanging piece: Ra1-c1 leaves the rook undefended to the b2 pawn.
  fromFen(p, "4k3/8/8/8/8/8/1p6/R3K3 w - -");
  MoveEval ev;
  Move hang = {0, 2, 0};                 // a1 -> c1
  CHECK(analyzeMove(p, hang, 3, 0, &ev));
  CHECK(ev.hangsPiece);
  CHECK(!ev.playedIsBest);
  CHECK(ev.cls == MC_BLUNDER);

  // Matt-in-1 vorhanden (Ta1-a8):
  fromFen(p, "6k1/5ppp/8/8/8/8/8/R5K1 w - -");
  Move mate = {0, 56, 0};                // a1 -> a8 (Matt)
  CHECK(analyzeMove(p, mate, 2, 0, &ev));
  CHECK(ev.playedIsBest);
  CHECK(ev.cls == MC_BEST);
  CHECK(!ev.hasReply);                    // checkmate -> no trainer response

  // ... and missing mate (Kg1-h1) results in "blunder" + missedMate.
  Move quiet = {6, 7, 0};                // g1 -> h1
  CHECK(analyzeMove(p, quiet, 2, 0, &ev));
  CHECK(ev.missedMate);
  CHECK(ev.cls == MC_BLUNDER);
  CHECK(ev.hasReply);                    // the game continues

  return 0;
}

// =============================================================================
// Sudoku
// =============================================================================
static int testSudoku() {
  using namespace sudoku;

  // solveFill: empty grid -> complete, conflict-free solution.
  uint8_t g[81];
  for (int i = 0; i < 81; i++) g[i] = 0;
  CHECK(solveFill(g, testRnd));
  for (int i = 0; i < 81; i++) CHECK(g[i] >= 1 && g[i] <= 9);
  // No conflicts: each digit fits when temporarily removed.
  for (int i = 0; i < 81; i++) {
    uint8_t v = g[i];
    g[i] = 0;
    CHECK(fits(g, i, v));
    g[i] = v;
  }

  // A complete grid has exactly one solution.
  CHECK(countSolutions(g, 5) == 1);

  // generate: every difficulty has a unique solution, a plausible clue count,
  // a valid sol[], and consistent given/cell values.
  const int diffs[3] = {EASY, MEDIUM, HARD};
  for (int d = 0; d < 3; d++) {
    Puzzle p;
    generate(p, cluesFor((uint8_t)diffs[d]), testRnd);

    int given = 0;
    for (int i = 0; i < 81; i++) {
      CHECK(p.given[i] == (p.cell[i] != 0));   // given <-> filled
      if (p.given[i]) { CHECK(p.cell[i] == p.sol[i]); given++; }  // clue is correct
      CHECK(p.sol[i] >= 1 && p.sol[i] <= 9);
    }
    CHECK(given >= cluesFor((uint8_t)diffs[d]));   // never below the target
    CHECK(given <= 81);

    // Uniqueness: the generated puzzle has exactly one solution.
    CHECK(countSolutions(p.cell, 2) == 1);

    // isSolved: an empty puzzle is unsolved; filling in the solution solves it.
    CHECK(!isSolved(p));
    Puzzle full = p;
    for (int i = 0; i < 81; i++) full.cell[i] = full.sol[i];
    CHECK(isSolved(full));
  }

  // conflict: detect duplicates in a row, column, or box.
  {
    Puzzle p;
    for (int i = 0; i < 81; i++) { p.cell[i] = 0; p.given[i] = false; }
    p.cell[0] = 5; p.cell[1] = 5;        // same row
    CHECK(conflict(p, 0) && conflict(p, 1));
    p.cell[1] = 0; p.cell[9] = 5;        // same column
    CHECK(conflict(p, 0) && conflict(p, 9));
    p.cell[9] = 0; p.cell[10] = 5;       // same box (cells 0 and 10)
    CHECK(conflict(p, 0) && conflict(p, 10));
    p.cell[10] = 0;
    CHECK(!conflict(p, 0));              // alone -> no conflict
  }

  printf("Sudoku: ok\n");
  return 0;
}

int main() {
  if (test2048()) return 1;
  if (testMines()) return 1;
  if (testTtt()) return 1;
  if (testChess()) return 1;
  if (testSudoku()) return 1;
  printf("All tests passed (%d checks).\n", s_checks);
  return 0;
}

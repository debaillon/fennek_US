// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// mathquiz_app.cpp — "Mental Math": menu (operation + level) and quiz screen.
// Logic and generation are Arduino-independent in mathquiz_core.h (host-tested).
//
// Controls: menu W/S = mode, A/D = level, Enter/tap = start. Quiz: type digits,
// Backspace deletes, Enter checks (press Enter again for the next problem),
// Q/Back = return. Typing digits updates only the answer strip (renderRegion),
// avoiding a full-screen flash on every key (see Reader/Notes).
// =============================================================================
#include "mathquiz_app.h"

#include <Arduino.h>
#include <GxEPD2_BW.h>   // GxEPD_BLACK / GxEPD_WHITE
#include <esp_random.h>
#include <string.h>

#include "config.h"
#include "core/display.h"
#include "core/gui.h"
#include "core/i18n.h"
#include "core/settings.h"
#include "apps/mathquiz_core.h"

namespace {

using gui::Rect;
using namespace mathquiz;

constexpr int W = EINK_W;

uint32_t rnd(uint32_t n) { return n ? esp_random() % n : 0; }

// --- Modes ---------------------------------------------------------------------
struct Mode { const char* label; uint16_t mask; };
const Mode kModes[] = {
  {"Plus",     opBit(ADD)},
  {"Minus",    opBit(SUB)},
  {"Multiply", opBit(MUL)},
  {"Divide",   opBit(DIV)},
  {"Mixed",    (uint16_t)(opBit(ADD) | opBit(SUB) | opBit(MUL) | opBit(DIV))},
  // CFO: share/margin, year-over-year growth, tax markup, discount, percent of, Rule of 72.
  {"CFO",      (uint16_t)(opBit(SHARE) | opBit(GROWTH) | opBit(MARKUP) |
                          opBit(DISCOUNT) | opBit(PCT) | opBit(RULE72))},
};
constexpr int kNumModes = sizeof(kModes) / sizeof(kModes[0]);
const char* const kLevels[3] = {"Easy", "Medium", "Hard"};

// --- Layout: menu (level row + 6 mode buttons, compact for 320 px height) -----
const Rect kLevelBtn{10, 64, 220, 30};
Rect modeBtn(int i) { return Rect{10, 100 + i * 35, 220, 30}; }

// --- Layout: Quiz -------------------------------------------------------------
constexpr int ANSW_Y = 132;   // Streifen Aufgabe-Ergebnis (Region-Refresh)
constexpr int ANSW_H = 108;

// --- State ---------------------------------------------------------------------
enum Screen { MENU, QUIZ };
Screen  s_screen = MENU;
int     s_sel = 4;            // Menu selection (default: Mixed)
uint8_t s_level = 1;          // 0..2 (displayed/active level for the mode)

// Adaptive difficulty: a continuous skill counter (0..3*kBand-1) moves with
// each answer and determines the level (= s_skill / kBand). Correct answers
// raise it slowly (~7 in a row per level); errors lower it faster (~3 in a row).
// When the level changes, the counter moves to the middle of the new band to
// provide hysteresis and prevent toggling around a boundary.
constexpr int kBand    = 100;   // Skill-Punkte pro Stufe
constexpr int kStepUp  = 7;     // pro richtiger Antwort
constexpr int kStepDn  = 18;    // Per incorrect answer (errors weigh more)
int  s_skill = 150;             // Start at the middle of level 1
bool s_levelChanged = false;    // Brief "Level: ..." feedback after a change

uint16_t s_mask = 0;          // aktive Operationsmaske im Quiz
Problem s_problem{};
char    s_input[10] = "";
int     s_inputLen = 0;
bool    s_answered = false;
bool    s_lastCorrect = false;
uint8_t s_lastVerdict = WRONG;   // Exact / estimate / incorrect (feedback text)

// Time target: solve each problem in <= 20 s. Time each problem and show the
// elapsed seconds in the feedback (no live countdown, avoiding E-Ink flicker).
constexpr uint32_t kTargetSecs = 20;
uint32_t s_qStartMs = 0;      // Start time for the current problem
uint32_t s_lastSecs = 0;      // Seconds used for the last answer

int s_total = 0, s_correct = 0, s_streak = 0;

// Derive the level from the skill counter (clamped to 0..2).
int levelFromSkill(int skill) {
  int lv = skill / kBand;
  return lv < 0 ? 0 : (lv > 2 ? 2 : lv);
}

// Load the saved adaptive level for the selected mode and put the counter in
// the middle of its band, so half a level must be earned before it changes.
void loadLevelForMode() {
  s_level = settings::mathLevel((uint8_t)s_sel);
  s_skill = s_level * kBand + kBand / 2;
}

// =============================================================================
// Menu
// =============================================================================
void menuDraw(Adafruit_GFX& g) {
  g.setTextColor(GxEPD_BLACK);
  gui::printAt(g, 10, 28, i18n::tr(i18n::Str::AppMath), 2);

  char sub[40];
  snprintf(sub, sizeof(sub), "Best streak: %u", settings::mathBestStreak());
  gui::printAt(g, 10, 52, sub, 1);

  // Level row.
  g.drawRoundRect(kLevelBtn.x, kLevelBtn.y, kLevelBtn.w, kLevelBtn.h, 6, GxEPD_BLACK);
  char lvl[32];
  snprintf(lvl, sizeof(lvl), "Level: %s", kLevels[s_level]);
  gui::printAt(g, kLevelBtn.x + 10, kLevelBtn.y + 7, lvl, 2);
  gui::printAt(g, kLevelBtn.x + kLevelBtn.w - 40, kLevelBtn.y + 11, "A/D", 1);

  for (int i = 0; i < kNumModes; i++) {
    Rect r = modeBtn(i);
    g.drawRoundRect(r.x, r.y, r.w, r.h, 6, GxEPD_BLACK);
    if (i == s_sel)
      g.drawRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 5, GxEPD_BLACK);
    gui::printAt(g, r.x + 12, r.y + 7, kModes[i].label, 2);
  }
  gui::printAt(g, 10, 309, "W/S mode  A/D level (adaptive)  Enter", 1);
}

void startQuiz() {
  s_mask = kModes[s_sel].mask;
  loadLevelForMode();          // Use this mode's adaptive level
  s_total = s_correct = s_streak = 0;
  s_input[0] = '\0';
  s_inputLen = 0;
  s_answered = false;
  s_levelChanged = false;
  s_problem = generate(pickOp(s_mask, rnd), s_level, rnd);
  s_qStartMs = millis();
  s_screen = QUIZ;
  Serial.printf("[MATH] Start: %s, level %s\n", kModes[s_sel].label, kLevels[s_level]);
  appmgr::markDirty();
}

// Manual level override (A/D or tap): override and save this mode's adaptive
// level. The skill counter then starts in the middle of the band.
void setLevelManual(uint8_t level) {
  s_level = level > 2 ? 2 : level;
  s_skill = s_level * kBand + kBand / 2;
  settings::setMathLevel((uint8_t)s_sel, s_level);
  appmgr::markDirty();
}

void menuInput(const InputEvent& e) {
  if (e.type == InputEvent::TAP) {
    if (kLevelBtn.hit(e.x, e.y)) { setLevelManual((s_level + 1) % 3); return; }
    for (int i = 0; i < kNumModes; i++)
      if (modeBtn(i).hit(e.x, e.y)) { s_sel = i; startQuiz(); return; }
    return;
  }
  switch (e.key) {
    case 'w': case 'W': case 'i': case 'I': s_sel = (s_sel + kNumModes - 1) % kNumModes; loadLevelForMode(); appmgr::markDirty(); break;
    case 's': case 'S': case 'k': case 'K': s_sel = (s_sel + 1) % kNumModes;            loadLevelForMode(); appmgr::markDirty(); break;
    case 'a': case 'A': case 'j': case 'J': setLevelManual((s_level + 2) % 3);          break;
    case 'd': case 'D': case 'l': case 'L': setLevelManual((s_level + 1) % 3);          break;
    case '\r':          startQuiz(); break;
    case '\b':
    case 'q': case 'Q': appmgr::goHome(); break;
    default: break;
  }
}

// =============================================================================
// Quiz
// =============================================================================
// Problem, answer, and feedback (middle strip), also drawn as a region.
void quizMid(Adafruit_GFX& g) {
  g.fillRect(0, ANSW_Y, W, ANSW_H, GxEPD_WHITE);
  g.setTextColor(GxEPD_BLACK);

  char q[48];
  switch (s_problem.op) {
    case PCT:
      snprintf(q, sizeof(q), "%ld%% von %ld =", (long)s_problem.b, (long)s_problem.a);
      break;
    case MARKUP:
      snprintf(q, sizeof(q), "%ld + %ld%% =", (long)s_problem.a, (long)s_problem.b);
      break;
    case DISCOUNT:
      snprintf(q, sizeof(q), "%ld - %ld%% =", (long)s_problem.a, (long)s_problem.b);
      break;
    case SHARE:   // a = part, b = base; find the percentage
      snprintf(q, sizeof(q), "%ld/%ld = ?%%", (long)s_problem.a, (long)s_problem.b);
      break;
    case GROWTH: {   // a -> end; find the growth percentage
      long end = (long)s_problem.a + (long)s_problem.a / 100 * (long)s_problem.b;
      snprintf(q, sizeof(q), "%ld>%ld = ?%%", (long)s_problem.a, end);
      break;
    }
    case RULE72:   // Doubling time in years at b% (Rule of 72)
      snprintf(q, sizeof(q), "2x at %ld%% = ?y", (long)s_problem.b);
      break;
    default:
      snprintf(q, sizeof(q), "%ld %c %ld =", (long)s_problem.a,
               opChar(s_problem.op), (long)s_problem.b);
      break;
  }
  constexpr int kMargin = 6;
  const int maxW = W - 2 * kMargin;
  uint16_t bw, bh;

  // Largest font size (maxSize..1) at which `text` fits in maxW. textBounds
  // uses the current TextSize, so setTextSize before each measurement.
  auto fitSize = [&](const char* text, uint8_t maxSize) -> uint8_t {
    for (uint8_t s = maxSize; s > 1; s--) {
      g.setTextSize(s);
      gui::textBounds(g, text, &bw, &bh);
      if ((int)bw <= maxW) return s;
    }
    return 1;
  };
  // Draw a centered line at `size` (remeasure at this size).
  auto drawCentered = [&](int y, const char* text, uint8_t size) {
    g.setTextSize(size);
    gui::textBounds(g, text, &bw, &bh);
    int x = (W - (int)bw) / 2;
    if (x < kMargin) x = kMargin;
    gui::printAt(g, x, y, text, size);
  };

  // Auto-scale the problem and answer (long CFO problems/answers shrink from
  // 3->2->1 instead of overflowing the edge).
  uint8_t qsize = fitSize(q, 3);

  char ans[16];
  snprintf(ans, sizeof(ans), "%s%s", s_input, s_answered ? "" : "_");
  const char* ansShown = ans[0] ? ans : " ";
  uint8_t asize = fitSize(ansShown, 3);

  char fb[72];
  if (s_answered) {
    char base[40];
    if (s_lastVerdict == EXACT)
      snprintf(base, sizeof(base), "Correct! (%lus)", (unsigned long)s_lastSecs);
    else if (s_lastVerdict == CLOSE)   // Good estimate: also show the exact value
      snprintf(base, sizeof(base), "Good estimate! %ld (%lus)",
               (long)s_problem.answer, (unsigned long)s_lastSecs);
    else
      snprintf(base, sizeof(base), "Incorrect: %ld (%lus)",
               (long)s_problem.answer, (unsigned long)s_lastSecs);
    // Briefly show the new level after an adaptive change.
    if (s_levelChanged) snprintf(fb, sizeof(fb), "%s  Level: %s", base, kLevels[s_level]);
    else snprintf(fb, sizeof(fb), "%s", base);
  }
  uint8_t fsize = s_answered ? fitSize(fb, 2) : 0;

  // Center vertically in the strip: calculate block height from the selected
  // sizes (9 px/line per size, as in drawWrapped), with gaps between lines.
  constexpr int gap = 12;
  int hQ = 9 * qsize, hA = 9 * asize, hF = s_answered ? 9 * fsize : 0;
  int blockH = hQ + gap + hA + (s_answered ? gap + hF : 0);
  int y = ANSW_Y + (ANSW_H - blockH) / 2;
  if (y < ANSW_Y + 2) y = ANSW_Y + 2;

  drawCentered(y, q, qsize);
  y += hQ + gap;
  drawCentered(y, ans, asize);   // ans without a cursor is never empty
  if (s_answered) {
    y += hA + gap;
    drawCentered(y, fb, fsize);
  }
}

void quizDraw(Adafruit_GFX& g) {
  g.setTextColor(GxEPD_BLACK);
  gui::printAt(g, 10, 26, kModes[s_sel].label, 2);
  gui::printAt(g, 150, 32, kLevels[s_level], 1);
  g.drawLine(0, 52, W, 52, GxEPD_BLACK);

  quizMid(g);

  char stat[48];
  snprintf(stat, sizeof(stat), "Streak: %d   Correct: %d/%d", s_streak, s_correct, s_total);
  gui::printAt(g, 10, 286, stat, 1);
  // Estimation problems (tol>0) accept close answers; show the time target.
  if (s_problem.tol > 0)
    gui::printAt(g, 10, 304, "Estimate it  Target <20s  Q=back", 1);
  else
    gui::printAt(g, 10, 304, "Digits + Enter  Target <20s  Q=back", 1);
}

void nextProblem() {
  s_input[0] = '\0';
  s_inputLen = 0;
  s_answered = false;
  s_levelChanged = false;
  s_problem = generate(pickOp(s_mask, rnd), s_level, rnd);
  s_qStartMs = millis();
  appmgr::markDirty();
}

// Update adaptive difficulty after each answer: move the counter, change the
// level if needed, and save it. Intentionally gradual: leveling up requires
// several correct answers in a row.
void adaptDifficulty() {
  s_skill += s_lastCorrect ? kStepUp : -kStepDn;
  if (s_skill < 0) s_skill = 0;
  if (s_skill > 3 * kBand - 1) s_skill = 3 * kBand - 1;
  int nl = levelFromSkill(s_skill);
  if (nl != s_level) {
    s_level = (uint8_t)nl;
    s_skill = nl * kBand + kBand / 2;     // Hysteresis: middle of the new level band
    settings::setMathLevel((uint8_t)s_sel, s_level);
    s_levelChanged = true;
    Serial.printf("[MATH] Level changed: %s\n", kLevels[s_level]);
  }
}

void submit() {
  if (s_inputLen == 0) return;
  long given = atol(s_input);
  s_lastSecs = (millis() - s_qStartMs) / 1000;
  Verdict v = accept(s_problem, (int32_t)given);
  s_lastVerdict = v;
  s_lastCorrect = (v != WRONG);   // A good estimate counts as correct
  s_total++;
  if (s_lastCorrect) {
    s_correct++;
    s_streak++;
    settings::setMathBestStreak((uint16_t)s_streak);
  } else {
    s_streak = 0;
  }
  adaptDifficulty();
  s_answered = true;
  appmgr::markDirty();   // Feedback + statistics (full refresh, once per problem)
}

char mapKeyToDigit(char k) {
  switch (k) {
    case 'w': case 'W': return '1';
    case 'e': case 'E': return '2';
    case 'r': case 'R': return '3';
    case 's': case 'S': return '4';
    case 'd': case 'D': return '5';
    case 'f': case 'F': return '6';
    case 'z': case 'Z': return '7';
    case 'x': case 'X': return '8';
    case 'c': case 'C': return '9';
    case 0x02:          return '0'; // Microphone key without Alt/Sym
    default:            return k;
  }
}

void quizInput(const InputEvent& e) {
  if (e.type == InputEvent::TAP) {
    if (s_answered) nextProblem();   // Tap after grading = continue
    return;
  }
  char key = mapKeyToDigit(e.key);
  if (key == 'q' || key == 'Q') { s_screen = MENU; appmgr::markDirty(); return; }
  if (key == '\b') {
    if (s_answered || s_inputLen == 0) { s_screen = MENU; appmgr::markDirty(); return; }
    s_input[--s_inputLen] = '\0';   // Delete the last digit
    display::renderRegion(quizMid, ANSW_Y, ANSW_H);
    return;
  }
  if (key == '\r') {
    if (s_answered) nextProblem();
    else submit();
    return;
  }
  if (key >= '0' && key <= '9' && !s_answered) {
    if (s_inputLen < (int)sizeof(s_input) - 2) {
      s_input[s_inputLen++] = key;
      s_input[s_inputLen] = '\0';
      display::renderRegion(quizMid, ANSW_Y, ANSW_H);
    }
  }
}

// =============================================================================
class MathQuizApp : public App {
 public:
  const char* id() const override { return "Kopfrechnen"; }
  const char* name() const override { return i18n::tr(i18n::Str::AppMath); }

  void onEnter() override { s_screen = MENU; loadLevelForMode(); }

  void handleInput(const InputEvent& e) override {
    if (s_screen == QUIZ) quizInput(e);
    else menuInput(e);
  }

  void draw(Adafruit_GFX& g) override {
    if (s_screen == QUIZ) quizDraw(g);
    else menuDraw(g);
  }
};

MathQuizApp s_app;

}  // namespace

namespace mathquiz_app {
App* get() { return &s_app; }
}  // namespace mathquiz_app

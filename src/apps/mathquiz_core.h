// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// mathquiz_core.h — mental math problem generator, Arduino-independent (host-testable).
//
// Deterministically generates an arithmetic problem for each operation and
// difficulty level (randomness is injected as a callback). Generation invariants:
//  - Subtraction NEVER produces a negative result (b <= a).
//  - Division is ALWAYS exact (a = quotient * b), with divisor >= 2.
// This lets the UI accept digits only, without minus signs or decimals.
//
// Time target: each problem should be solvable in about 20 s. Number ranges
// are intentionally limited, and problems solved approximately in your head
// (large products, monetary percentages) have an estimate window `tol`: an
// answer within ±tol counts as a good estimate (see accept()). Quick, exact
// problems (addition/subtraction, small multiplication, exact division, and
// Rule of 72) keep tol=0 and still require the exact value.
// =============================================================================
#pragma once

#include <stdint.h>

namespace mathquiz {

// Basic arithmetic + CFO/finance operations. For money operations, the base
// amount is always a multiple of 100, producing integer results. Fields a/b
// have different meanings per operation (see comments); the UI formats the
// problem text accordingly.
enum Op : uint8_t {
  ADD = 0, SUB = 1, MUL = 2, DIV = 3,
  PCT = 4,        // "b% of a"         -> a*b/100
  MARKUP = 5,     // "a + b%"          -> a*(100+b)/100   (net + tax)
  DISCOUNT = 6,   // "a - b%"          -> a*(100-b)/100   (discount)
  SHARE = 7,      // "a/b = ?%"        -> a*100/b          (share/margin in %)
  GROWTH = 8,     // "a > end = ?%"    -> growth from a to end in %
  RULE72 = 9,     // "2x at b% = ?y"   -> 72/b (doubling time, Rule of 72)
  OP_COUNT = 10
};

// Operation bitmask (16-bit because there are more than 8 operations).
inline uint16_t opBit(Op o) { return (uint16_t)(1u << o); }

// Returns 0..n-1 (device: esp_random()%n; host: deterministic stub).
using RndFn = uint32_t (*)(uint32_t);

struct Problem {
  int32_t a, b;     // Operands (for DIV, a / b)
  uint8_t op;       // Operation
  int32_t answer;
  int32_t tol;      // Allowed estimate window (absolute difference; 0 = exact)
};

// Answer grade.
enum Verdict : uint8_t { WRONG = 0, EXACT = 1, CLOSE = 2 };

struct Range { int32_t lo, hi; };

namespace detail {
// Integer in [lo, hi] (inclusive).
inline int32_t pick(RndFn rnd, int32_t lo, int32_t hi) {
  if (hi <= lo) return lo;
  return lo + (int32_t)rnd((uint32_t)(hi - lo + 1));
}
// pct percent of the amount, but at least 1 (otherwise a small value has no window).
inline int32_t pctOf(int32_t value, int pct) {
  int32_t v = value < 0 ? -value : value;
  int32_t t = v * pct / 100;
  return t < 1 ? 1 : t;
}
}  // namespace detail

// Estimate window (absolute difference) for a problem. Problems solved only
// approximately in your head get a percentage window that grows with level;
// mental arithmetic problems requiring an exact answer keep a window of 0.
inline int32_t toleranceFor(uint8_t op, uint8_t level, int32_t answer) {
  switch (op) {
    case MUL:                                  // Large multiplication: estimates allowed from medium level
      return level == 0 ? 0 : detail::pctOf(answer, level == 1 ? 5 : 10);
    case PCT: case MARKUP: case DISCOUNT:      // Monetary amounts: estimates always allowed
      return detail::pctOf(answer, level == 0 ? 2 : (level == 1 ? 4 : 7));
    case SHARE: case GROWTH:                    // Percentage result: ±1..2 points
      return level == 2 ? 2 : 1;
    default:                                     // ADD/SUB/DIV/RULE72: exact
      return 0;
  }
}

// Generate a problem for operation op and level (0=Easy..2=Hard).
inline Problem generate(Op op, uint8_t level, RndFn rnd) {
  if (level > 2) level = 2;
  Problem p{};
  p.op = op;
  switch (op) {
    case ADD: {
      static const Range r[3] = {{1, 20}, {2, 100}, {10, 1000}};
      p.a = detail::pick(rnd, r[level].lo, r[level].hi);
      p.b = detail::pick(rnd, r[level].lo, r[level].hi);
      p.answer = p.a + p.b;
      break;
    }
    case SUB: {
      static const Range r[3] = {{1, 20}, {2, 100}, {10, 1000}};
      p.a = detail::pick(rnd, r[level].lo, r[level].hi);
      p.b = detail::pick(rnd, r[level].lo, p.a);   // b<=a -> no negative result
      p.answer = p.a - p.b;
      break;
    }
    case MUL: {
      // Limited ranges (20 s target); large products can be estimated from medium level.
      static const Range ra[3] = {{1, 10}, {2, 12}, {2, 19}};
      static const Range rb[3] = {{1, 10}, {2, 15}, {2, 30}};
      p.a = detail::pick(rnd, ra[level].lo, ra[level].hi);
      p.b = detail::pick(rnd, rb[level].lo, rb[level].hi);
      p.answer = p.a * p.b;
      break;
    }
    case DIV: {
      // Choose integer quotient q and divisor b; a = q*b, so division is exact.
      // Keep the divisor small (<=12) for quick mental division.
      static const Range rb[3] = {{2, 9}, {2, 10}, {2, 12}};
      static const Range rq[3] = {{1, 10}, {2, 12}, {2, 30}};
      p.b = detail::pick(rnd, rb[level].lo, rb[level].hi);
      int32_t q = detail::pick(rnd, rq[level].lo, rq[level].hi);
      p.a = q * p.b;
      p.answer = q;
      break;
    }
    case PCT:
    case MARKUP:
    case DISCOUNT:
    case SHARE:
    case GROWTH: {
      // Base amount G = 100*K (larger monetary values) and a percentage from a
      // realistic set. Since G is a multiple of 100, G*pct/100 = K*pct is an integer.
      static const Range rk[3] = {{1, 20}, {1, 100}, {2, 500}};   // -> G = 100..50000
      static const int8_t pEasy[] = {10, 20, 25, 50};
      static const int8_t pMed[]  = {5, 10, 15, 20, 25, 50};
      static const int8_t pHard[] = {3, 5, 7, 15, 19, 25, 30, 40};
      const int8_t* ps = pEasy; int pn = 4;
      if (level == 1) { ps = pMed;  pn = 6; }
      else if (level == 2) { ps = pHard; pn = 8; }
      int32_t K = detail::pick(rnd, rk[level].lo, rk[level].hi);
      int32_t pct = ps[rnd((uint32_t)pn)];
      int32_t G = 100 * K;
      int32_t part = K * pct;          // = G * pct / 100
      switch (op) {
        case PCT:      p.a = G;    p.b = pct; p.answer = part;       break;
        case MARKUP:   p.a = G;    p.b = pct; p.answer = G + part;   break;
        case DISCOUNT: p.a = G;    p.b = pct; p.answer = G - part;   break;  // pct<100 -> >=0
        case SHARE:    p.a = part; p.b = G;   p.answer = pct;        break;  // "part/G = ?%"
        default:       p.a = G;    p.b = pct; p.answer = pct;        break;  // GROWTH: a -> a+part
      }
      break;
    }
    case RULE72: {
      // Doubling time using the Rule of 72: years ~= 72 / interest rate.
      // Choose only divisors of 72 to get an integer number of years.
      static const int8_t rates[] = {2, 3, 4, 6, 8, 9, 12};
      int32_t r = rates[rnd(7)];
      p.a = 0;
      p.b = r;
      p.answer = 72 / r;
      break;
    }
    default: break;
  }
  p.tol = toleranceFor(p.op, level, p.answer);
  return p;
}

// Grade an answer: exact, good estimate (within ±tol), or incorrect. A good
// estimate counts as a correct answer in the statistics.
inline Verdict accept(const Problem& p, int32_t given) {
  if (given == p.answer) return EXACT;
  int32_t d = given - p.answer;
  if (d < 0) d = -d;
  return (p.tol > 0 && d <= p.tol) ? CLOSE : WRONG;
}

// Randomly choose an allowed operation from an operation bitmask.
inline Op pickOp(uint16_t mask, RndFn rnd) {
  Op ops[OP_COUNT];
  int n = 0;
  for (uint8_t i = 0; i < OP_COUNT; i++)
    if (mask & opBit((Op)i)) ops[n++] = (Op)i;
  if (n == 0) return ADD;
  return ops[rnd((uint32_t)n)];
}

// Arithmetic symbols for display (CP437-safe: 'x' and ':' instead of ×/÷).
inline char opChar(uint8_t op) {
  switch (op) {
    case ADD: return '+';
    case SUB: return '-';
    case MUL: return 'x';
    case DIV: return ':';
    case PCT: case MARKUP: case DISCOUNT:
    case SHARE: case GROWTH: case RULE72: return '%';
  }
  return '?';
}

}  // namespace mathquiz

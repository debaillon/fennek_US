// =============================================================================
// host_test_apps.cpp — host tests for the Arduino-free app cores
// (mathquiz_core, flashcards_core, podcast_core, reinschrift_core, ical_core).
// Not part of the firmware build.
//
//   g++ -std=c++17 -O2 -I src tools/host_test_apps.cpp -o /tmp/apps_test && /tmp/apps_test
//
// See also tools/host_test_games.cpp for host tests of the game cores.
// =============================================================================
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "apps/mathquiz_core.h"
#include "apps/flashcards_core.h"
#include "apps/podcast_core.h"
#include "apps/reinschrift_core.h"
#include "apps/ical_core.h"

// --- Deterministic random numbers (LCG) --------------------------------------
static uint32_t s_seed = 2025;
static uint32_t testRnd(uint32_t n) {
  s_seed = s_seed * 1664525u + 1013904223u;
  return n ? (s_seed >> 16) % n : 0;
}

static int s_checks = 0;
#define CHECK(cond) do { \
  if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } \
  s_checks++; \
} while (0)

// =============================================================================
// mathquiz
// =============================================================================
static int testMathquiz() {
  using namespace mathquiz;
  // Generate each basic operation at every level often enough to check invariants.
  for (int op = ADD; op <= DIV; op++) {
    for (uint8_t lvl = 0; lvl < 3; lvl++) {
      for (int i = 0; i < 5000; i++) {
        Problem p = generate((Op)op, lvl, testRnd);
        CHECK(p.op == op);
        switch (op) {
          case ADD: CHECK(p.answer == p.a + p.b); break;
          case SUB:
            CHECK(p.b <= p.a);          // nie negativ
            CHECK(p.answer == p.a - p.b);
            CHECK(p.answer >= 0);
            break;
          case MUL: CHECK(p.answer == p.a * p.b); break;
          case DIV:
            CHECK(p.b >= 2);            // Divisor sinnvoll
            CHECK(p.a % p.b == 0);      // divides evenly
            CHECK(p.answer == p.a / p.b);
            CHECK(p.answer >= 1);
            break;
        }
      }
    }
  }

  // Money operations (PCT/MARKUP/DISCOUNT): base amount a is divisible by 100.
  for (int op = PCT; op <= DISCOUNT; op++) {
    for (uint8_t lvl = 0; lvl < 3; lvl++) {
      for (int i = 0; i < 5000; i++) {
        Problem p = generate((Op)op, lvl, testRnd);
        CHECK(p.op == op);
        CHECK(p.a % 100 == 0);              // base amount is a multiple of 100
        CHECK(p.b >= 1 && p.b <= 99);       // valid percentage
        int32_t part = p.a / 100 * p.b;     // exakt, da a % 100 == 0
        if (op == PCT)         CHECK(p.answer == part);
        else if (op == MARKUP) CHECK(p.answer == p.a + part);
        else                   CHECK(p.answer == p.a - part);
        CHECK(p.answer >= 0);               // discount cannot be negative
      }
    }
  }

  // CFO SHARE/GROWTH: Basis durch 100 teilbar, Prozent exakt ganzzahlig.
  for (int op = SHARE; op <= GROWTH; op++) {
    for (uint8_t lvl = 0; lvl < 3; lvl++) {
      for (int i = 0; i < 5000; i++) {
        Problem p = generate((Op)op, lvl, testRnd);
        CHECK(p.op == op);
        CHECK(p.answer >= 1 && p.answer <= 99);
        if (op == SHARE) {
          // a = Anteil, b = Basis (Vielfaches von 100); answer = a*100/b exakt.
          CHECK(p.b % 100 == 0);
          CHECK((long)p.a * 100 % p.b == 0);
          CHECK(p.answer == (long)p.a * 100 / p.b);
          CHECK(p.a <= p.b);
        } else {  // GROWTH: a Basis, answer = Wachstum%, end = a + a/100*answer
          CHECK(p.a % 100 == 0);
          CHECK(p.answer == p.b);
        }
      }
    }
  }

  // CFO RULE72: doubling time = 72 / interest rate; use factors of 72 only.
  for (uint8_t lvl = 0; lvl < 3; lvl++) {
    for (int i = 0; i < 3000; i++) {
      Problem p = generate(RULE72, lvl, testRnd);
      CHECK(p.op == RULE72);
      CHECK(p.b >= 2 && 72 % p.b == 0);
      CHECK(p.answer == 72 / p.b);
      CHECK(p.answer >= 1);
    }
  }
  // --- Estimate tolerance + accept() ----------------------------------------
  // Exact problems (addition/subtraction/division/Rule72) have no estimate range.
  for (uint8_t lvl = 0; lvl < 3; lvl++) {
    for (int i = 0; i < 500; i++) {
      CHECK(generate(ADD, lvl, testRnd).tol == 0);
      CHECK(generate(SUB, lvl, testRnd).tol == 0);
      CHECK(generate(DIV, lvl, testRnd).tol == 0);
      CHECK(generate(RULE72, lvl, testRnd).tol == 0);
    }
  }
  // Multiplication: easy is exact (small 1×1); medium and hard allow estimates.
  for (int i = 0; i < 500; i++) {
    CHECK(generate(MUL, 0, testRnd).tol == 0);
    CHECK(generate(MUL, 1, testRnd).tol >= 1);
    CHECK(generate(MUL, 2, testRnd).tol >= 1);
  }
  // Money operations always allow a range; SHARE/GROWTH allow ±1–2 percentage points.
  for (uint8_t lvl = 0; lvl < 3; lvl++) {
    for (int op = PCT; op <= DISCOUNT; op++)
      CHECK(generate((Op)op, lvl, testRnd).tol >= 1);
    CHECK(generate(SHARE, lvl, testRnd).tol >= 1 && generate(SHARE, lvl, testRnd).tol <= 2);
    CHECK(generate(GROWTH, lvl, testRnd).tol >= 1 && generate(GROWTH, lvl, testRnd).tol <= 2);
  }
  // accept(): exact match, CLOSE at the range boundary, WRONG just beyond it.
  {
    Problem ex{}; ex.op = ADD; ex.answer = 42; ex.tol = 0;
    CHECK(accept(ex, 42) == EXACT);
    CHECK(accept(ex, 43) == WRONG);            // without a range, only exact matches count
    Problem es{}; es.op = MUL; es.answer = 700; es.tol = 70;
    CHECK(accept(es, 700) == EXACT);
    CHECK(accept(es, 770) == CLOSE);           // exactly at the boundary
    CHECK(accept(es, 630) == CLOSE);           // also below the answer
    CHECK(accept(es, 771) == WRONG);           // just outside the range
    CHECK(accept(es, 500) == WRONG);
  }

  // pickOp respects the mask.
  for (int i = 0; i < 1000; i++) {
    Op o = pickOp(opBit(MUL) | opBit(DIV), testRnd);
    CHECK(o == MUL || o == DIV);
  }
  CHECK(pickOp(opBit(SUB), testRnd) == SUB);
  CHECK(pickOp(0, testRnd) == ADD);   // empty mask -> fallback
  for (int i = 0; i < 1000; i++) {    // Finanz-Maske
    Op o = pickOp(opBit(PCT) | opBit(MARKUP) | opBit(DISCOUNT), testRnd);
    CHECK(o == PCT || o == MARKUP || o == DISCOUNT);
  }
  CHECK(opChar(ADD) == '+' && opChar(SUB) == '-');
  CHECK(opChar(MUL) == 'x' && opChar(DIV) == ':');
  CHECK(opChar(PCT) == '%' && opChar(MARKUP) == '%' && opChar(DISCOUNT) == '%');
  CHECK(opChar(SHARE) == '%' && opChar(GROWTH) == '%' && opChar(RULE72) == '%');
  printf("  mathquiz ok\n");
  return 0;
}

// =============================================================================
// flashcards
// =============================================================================
static int testFlashcards() {
  using namespace flashcards;
  Card c;

  // Tab separator.
  CHECK(parseLine("hund\tder Hund", &c));
  CHECK(strcmp(c.front, "hund") == 0);
  CHECK(strcmp(c.back, "der Hund") == 0);

  // Pipe separator + whitespace trimming.
  CHECK(parseLine("  katt   |   die Katze  ", &c));
  CHECK(strcmp(c.front, "katt") == 0);
  CHECK(strcmp(c.back, "die Katze") == 0);

  // Multiple words per side; a tab takes precedence over a later pipe.
  CHECK(parseLine("god morgon\tguten Morgen | wörtlich", &c));
  CHECK(strcmp(c.front, "god morgon") == 0);
  CHECK(strcmp(c.back, "guten Morgen | wörtlich") == 0);

  // Comments, blank lines, missing separator, or empty side are rejected.
  CHECK(!parseLine("# Schwedisch Vokabeln", &c));
  CHECK(!parseLine("", &c));
  CHECK(!parseLine("   ", &c));
  CHECK(!parseLine("ohne trenner", &c));
  CHECK(!parseLine("\tnur rueckseite", &c));
  CHECK(!parseLine("nur vorderseite\t", &c));

  // The following line must not be consumed (end-of-line boundary).
  CHECK(parseLine("a\tb\n# c | d", &c));
  CHECK(strcmp(c.front, "a") == 0);
  CHECK(strcmp(c.back, "b") == 0);

  // Leitner: correct advances a card (up to 4); incorrect returns it to 0.
  CHECK(nextBox(0, true) == 1);
  CHECK(nextBox(3, true) == 4);
  CHECK(nextBox(4, true) == 4);       // Decke
  CHECK(nextBox(4, false) == 0);
  CHECK(nextBox(2, false) == 0);

  // Intervals increase monotonically; box 0 is due immediately.
  CHECK(boxInterval(0) == 0);
  CHECK(boxInterval(1) < boxInterval(2));
  CHECK(boxInterval(2) < boxInterval(3));
  CHECK(boxInterval(3) < boxInterval(4));

  // Due dates.
  uint32_t now = 1700000000u;
  CHECK(isDue(0, 0, now));                          // new card
  CHECK(isDue(0, now - 10, now));                   // Box 0: due again immediately
  CHECK(!isDue(1, now - 100, now));                 // Box 1: not due yet
  CHECK(isDue(1, now - 2 * 86400u, now));           // Box 1: due after 2 days
  CHECK(!isDue(4, now - 10 * 86400u, now));         // Box 4: requires 16 days
  CHECK(isDue(4, now - 20 * 86400u, now));

  // cardKey is stable and differs for different fronts.
  CHECK(cardKey("hund") == cardKey("hund"));
  CHECK(cardKey("hund") != cardKey("katt"));

  // shuffle: the permutation preserves the multiset (no index lost or duplicated).
  int idx[12];
  for (int i = 0; i < 12; i++) idx[i] = i;
  shuffle(idx, 12, testRnd);
  int seen[12] = {0};
  for (int i = 0; i < 12; i++) { CHECK(idx[i] >= 0 && idx[i] < 12); seen[idx[i]]++; }
  for (int i = 0; i < 12; i++) CHECK(seen[i] == 1);
  // ... and actually changes the order (deterministic seed).
  int ident = 1;
  for (int i = 0; i < 12; i++) if (idx[i] != i) ident = 0;
  CHECK(!ident);

  // stableSortByBox: ascending box order, stable within each box.
  // Index-to-box mapping: 1 and 4 share box 0, so 1 must remain before 4.
  int j2[6] = {0, 1, 2, 3, 4, 5};
  uint8_t boxes[6] = {2, 0, 2, 1, 0, 1};
  stableSortByBox(j2, 6, [&](int i) { return boxes[i]; });
  for (int i = 1; i < 6; i++) CHECK(boxes[j2[i - 1]] <= boxes[j2[i]]);   // monotonic
  // First two entries are box-0 cards in input order (1, then 4).
  CHECK(j2[0] == 1 && j2[1] == 4);

  printf("  flashcards ok\n");
  return 0;
}

// =============================================================================
// podcast
// =============================================================================
static int testPodcast() {
  using namespace podcast;

  // Typical podcast feed (newest episode first), with CDATA, entities, and
  // a large description AFTER the enclosure (as in Freakshow).
  const char* feed =
    "<?xml version=\"1.0\"?><rss><channel><title>Freakshow</title>"
    "<item>"
    "<title>FS280 Tofu &amp; Trüffel</title>"
    "<guid isPermaLink=\"false\">freakshow-280</guid>"
    "<enclosure url=\"https://cdn.example.com/fs/fs280.mp3?t=1\" length=\"123456789\" type=\"audio/mpeg\"/>"
    "<description><![CDATA[ Sehr langer Text mit <b>HTML</b> ... ]]></description>"
    "</item>"
    "<item>"
    "<title>FS279 Alt</title>"
    "<guid>freakshow-279</guid>"
    "<enclosure url=\"https://cdn.example.com/fs/fs279.mp3\" length=\"111\" type=\"audio/mpeg\"/>"
    "</item>"
    "</channel></rss>";

  EpisodeMeta m = parseLatest(feed);
  CHECK(m.valid);
  CHECK(isComplete(m));
  CHECK(strcmp(m.title, "FS280 Tofu & Trüffel") == 0);   // &amp; decoded
  CHECK(strcmp(m.guid, "freakshow-280") == 0);            // first item, not 279
  CHECK(strcmp(m.url, "https://cdn.example.com/fs/fs280.mp3?t=1") == 0);
  CHECK(m.lengthBytes == 123456789UL);

  // CDATA title, reordered attributes, and &amp; in the URL.
  const char* feed2 =
    "<rss><channel><item>"
    "<enclosure type=\"audio/mpeg\" length=\"42\" url=\"http://h.fm/a.mp3?x=1&amp;y=2\"/>"
    "<title><![CDATA[Hallo Welt]]></title>"
    "<guid>g1</guid>"
    "</item></channel></rss>";
  EpisodeMeta m2 = parseLatest(feed2);
  CHECK(m2.valid);
  CHECK(strcmp(m2.title, "Hallo Welt") == 0);
  CHECK(strcmp(m2.url, "http://h.fm/a.mp3?x=1&y=2") == 0);   // &amp; -> &
  CHECK(m2.lengthBytes == 42UL);

  // No <guid> → fall back to URL.
  const char* feed3 =
    "<rss><channel><item><title>T</title>"
    "<enclosure url=\"http://h.fm/b.mp3\"/></item></channel></rss>";
  EpisodeMeta m3 = parseLatest(feed3);
  CHECK(m3.valid);
  CHECK(strcmp(m3.guid, "http://h.fm/b.mp3") == 0);

  // Missing <item> or <enclosure> → invalid.
  CHECK(!parseLatest("<rss><channel><title>x</title></channel></rss>").valid);
  CHECK(!parseLatest("<rss><channel><item><title>x</title></item></channel></rss>").valid);

  // Incomplete buffer (description truncated), but enclosure is present:
  // isComplete remains true, so the stream loader may stop.
  const char* partial =
    "<rss><channel><item><title>Teil</title><guid>g</guid>"
    "<enclosure url=\"http://h.fm/c.mp3\" length=\"5\"/>"
    "<description><![CDATA[ angeschnitten ...";
  CHECK(isComplete(parseLatest(partial)));

  // feedSlug: readable host prefix; deterministic and unique per URL.
  char s1[40], s2[40], s3[40];
  feedSlug("https://freakshow.fm/feed/mp3/", s1, sizeof(s1));
  feedSlug("https://freakshow.fm/feed/m4a/", s2, sizeof(s2));
  feedSlug("https://freakshow.fm/feed/mp3/", s3, sizeof(s3));
  CHECK(strncmp(s1, "freakshowfm-", 12) == 0);
  CHECK(strcmp(s1, s3) == 0);    // deterministisch
  CHECK(strcmp(s1, s2) != 0);    // anderer Pfad -> anderer Slug
  CHECK(strchr(s1, '/') == nullptr && strchr(s1, '.') == nullptr);

  // urlExt: get extension from URL; ignore query/fragment; default to .mp3.
  char e[8];
  urlExt("http://h.fm/a.mp3?t=1", e, sizeof(e));   CHECK(strcmp(e, ".mp3") == 0);
  urlExt("http://h.fm/a.m4a", e, sizeof(e));        CHECK(strcmp(e, ".m4a") == 0);
  urlExt("http://h.fm/stream", e, sizeof(e));       CHECK(strcmp(e, ".mp3") == 0);
  urlExt("http://h.fm/x.weird", e, sizeof(e));      CHECK(strcmp(e, ".mp3") == 0);

  printf("  podcast ok\n");
  return 0;
}

// =============================================================================
// reinschrift (Todo-Parser + konfliktsicherer Merge)
// =============================================================================
static int testReinschrift() {
  using namespace reinschrift;
  Task t;

  // Parse a complete line.
  CHECK(parseLine("- [ ] Putzen Fenster +Haushalt @Daheim due:2026-06-27T12:00 ~note:\"Fenster putzen\" ^Y64JiBDu", &t));
  CHECK(!t.done);
  CHECK(strcmp(t.title, "Putzen Fenster") == 0);
  CHECK(strcmp(t.topic, "Haushalt") == 0);
  CHECK(strcmp(t.context, "Daheim") == 0);
  CHECK(strcmp(t.marker, "Y64JiBDu") == 0);
  CHECK(t.dueEpoch != 0 && t.dueEpoch != kSomeday);

  // Completed + "someday" (year 9999) + wiki link instead of note.
  CHECK(parseLine("- [x] Sense entrosten +Sense @Keller due:9999-12-31T00:00 [[2025.md]] ^swbu95", &t));
  CHECK(t.done);
  CHECK(strcmp(t.title, "Sense entrosten") == 0);
  CHECK(isSomeday(t.dueEpoch));

  // No attributes → the entire remainder is the title.
  CHECK(parseLine("- [ ] Einfach nur Text", &t));
  CHECK(strcmp(t.title, "Einfach nur Text") == 0);
  CHECK(t.dueEpoch == 0 && t.marker[0] == '\0');

  // Headers and non-task lines → false.
  CHECK(!parseLine("### +Haushalt", &t));
  CHECK(!parseLine("# Zentrale Aufgabenübersicht", &t));
  CHECK(!parseLine("", &t));
  CHECK(!parseLine("nur text", &t));

  // Recognize topic headers.
  char topic[kTopicLen];
  CHECK(parseTopicHeader("### +Haushalt", topic, sizeof(topic)));
  CHECK(strcmp(topic, "Haushalt") == 0);
  CHECK(!parseTopicHeader("## Kein Plus", topic, sizeof(topic)));
  CHECK(!parseTopicHeader("- [ ] x +Topic ^a", topic, sizeof(topic)));

  // Due dates relative to "now" (day precision).
  uint32_t day = 86400u;
  uint32_t now = 20000u * day + 5000u;            // an arbitrary day, around noon
  CHECK(isDueToday(19999u * day, now));            // due yesterday → due today
  CHECK(isDueToday(20000u * day, now));            // heute
  CHECK(!isDueToday(20001u * day, now));           // tomorrow
  CHECK(isOverdue(19999u * day, now));
  CHECK(!isOverdue(20000u * day, now));
  CHECK(!isOverdue(kSomeday, now) && !isDueToday(kSomeday, now));

  // --- Conflict-safe merge ---------------------------------------------------
  const char* remote =
    "# Zentrale Aufgabenübersicht\n"
    "### +Haushalt\n"
    "- [ ] Fenster putzen +Haushalt due:2026-06-27T12:00 ^aaa111\n"
    "- [ ] Müll raus +Haushalt due:2026-06-21T12:00 ^bbb222\n"
    "### +IT\n"
    "- [ ] Backups prüfen +IT due:9999-12-31T00:00 ^ccc333\n";

  // Local operations: complete aaa111, move ccc333 to tomorrow, add a task under +Haushalt.
  Op ops[3];
  memset(ops, 0, sizeof(ops));
  ops[0].kind = OP_TOGGLE; strcpy(ops[0].marker, "aaa111"); ops[0].done = true;
  ops[1].kind = OP_DUE;    strcpy(ops[1].marker, "ccc333"); strcpy(ops[1].value, "2026-06-23T12:00");
  ops[2].kind = OP_ADD;    strcpy(ops[2].topic, "Haushalt");
  strcpy(ops[2].line, "- [ ] Staubsaugen +Haushalt ^new999");

  char out[2048];
  CHECK(applyOps(remote, ops, 3, out, sizeof(out)));
  CHECK(strstr(out, "- [x] Fenster putzen +Haushalt due:2026-06-27T12:00 ^aaa111") != nullptr);  // completed
  CHECK(strstr(out, "Müll raus") != nullptr);                       // unrelated line unchanged
  CHECK(strstr(out, "- [x] Müll raus") == nullptr);                 // not completed along with it
  CHECK(strstr(out, "Backups prüfen +IT due:2026-06-23T12:00") != nullptr);  // due date replaced, not duplicated
  CHECK(strstr(out, "due:9999") == nullptr);
  CHECK(strstr(out, "- [ ] Staubsaugen +Haushalt ^new999") != nullptr);      // new task is present
  // under the correct header (before "### +IT").
  CHECK(strstr(out, "Staubsaugen") < strstr(out, "### +IT"));

  // Conflict scenario: another device DELETED aaa111 (marker is absent
  // remotely). Our toggle operation must not damage anything and is discarded.
  const char* remote2 =
    "### +Haushalt\n"
    "- [ ] Müll raus +Haushalt ^bbb222\n";
  Op ops2[1]; memset(ops2, 0, sizeof(ops2));
  ops2[0].kind = OP_TOGGLE; strcpy(ops2[0].marker, "aaa111"); ops2[0].done = true;
  char out2[1024];
  CHECK(applyOps(remote2, ops2, 1, out2, sizeof(out2)));
  CHECK(strstr(out2, "Müll raus") != nullptr);     // remote state is preserved
  CHECK(strstr(out2, "aaa111") == nullptr);          // do not recreate a missing operation

  // ADD with no existing topic → append a new topic.
  Op ops3[1]; memset(ops3, 0, sizeof(ops3));
  ops3[0].kind = OP_ADD; strcpy(ops3[0].topic, "Neu"); strcpy(ops3[0].line, "- [ ] Frisch +Neu ^z9");
  char out3[1024];
  CHECK(applyOps(remote2, ops3, 1, out3, sizeof(out3)));
  CHECK(strstr(out3, "### +Neu") != nullptr);
  CHECK(strstr(out3, "- [ ] Frisch +Neu ^z9") != nullptr);

  // Marker generation: 8 Base62 characters, deterministic with the test RNG.
  char mk[kMarkerLen];
  genMarker(mk, sizeof(mk), testRnd);
  CHECK(strlen(mk) == 8);
  for (int i = 0; mk[i]; i++) {
    char c = mk[i];
    CHECK((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
  }

  printf("  reinschrift ok\n");
  return 0;
}

// =============================================================================
// ical (Kalender-Parser + RRULE-Expansion)
// =============================================================================
static int feedAll(ical::Parser& p, const char* ics, ical::Event* events, int maxEv) {
  int n = 0;
  const char* line = ics;
  char buf[512];
  while (*line) {
    const char* e = line;
    while (*e && *e != '\n') e++;
    size_t len = (size_t)(e - line);
    if (len > 0 && line[len - 1] == '\r') len--;     // CRLF → drop CR
    if (len > sizeof(buf) - 1) len = sizeof(buf) - 1;
    memcpy(buf, line, len); buf[len] = '\0';
    ical::Event ev;
    if (p.feedLine(buf, &ev) && n < maxEv) events[n++] = ev;
    line = (*e == '\n') ? e + 1 : e;
  }
  ical::Event ev;
  if (p.finish(&ev) && n < maxEv) events[n++] = ev;
  return n;
}

static int testIcal() {
  using namespace ical;

  // Two events: one all-day, one with a date/time; folded SUMMARY line.
  const char* ics =
    "BEGIN:VCALENDAR\r\n"
    "VERSION:2.0\r\n"
    "BEGIN:VEVENT\r\n"
    "UID:abc-1\r\n"
    "SUMMARY:Zahnarzt Termin sehr lang gefa\r\n"
    " ltet und entfaltet\r\n"
    "DTSTART:20260622T090000Z\r\n"
    "DTEND:20260622T100000Z\r\n"
    "LOCATION:Praxis\r\n"
    "END:VEVENT\r\n"
    "BEGIN:VEVENT\r\n"
    "UID:abc-2\r\n"
    "SUMMARY:Geburtstag\r\n"
    "DTSTART;VALUE=DATE:20260625\r\n"
    "END:VEVENT\r\n"
    "END:VCALENDAR\r\n";

  Parser p;
  Event evs[8];
  int n = feedAll(p, ics, evs, 8);
  CHECK(n == 2);
  CHECK(strcmp(evs[0].summary, "Zahnarzt Termin sehr lang gefaltet und entfaltet") == 0);
  CHECK(!evs[0].allDay);
  CHECK(strcmp(evs[0].location, "Praxis") == 0);
  CHECK(evs[0].end > evs[0].start);
  CHECK(evs[0].end - evs[0].start == 3600);   // 1 hour
  CHECK(evs[1].allDay);
  CHECK(strcmp(evs[1].summary, "Geburtstag") == 0);

  // Verify the 2026-06-22T09:00:00Z epoch using the known day count.
  uint32_t start22 = (uint32_t)(detail::daysFromCivil(2026, 6, 22) * 86400L) + 9 * 3600;
  CHECK(evs[0].start == start22);

  // One-time event only within the window.
  int count = 0;
  expand(evs[0], start22 - 86400, start22 + 86400, [&](uint32_t, uint32_t) { count++; });
  CHECK(count == 1);
  count = 0;
  expand(evs[0], start22 + 7 * 86400, start22 + 14 * 86400, [&](uint32_t, uint32_t) { count++; });
  CHECK(count == 0);   // outside the window

  // Weekly recurrence, limited by COUNT and by the time window.
  const char* rec =
    "BEGIN:VEVENT\r\n"
    "SUMMARY:Standup\r\n"
    "DTSTART:20260601T080000Z\r\n"
    "DTEND:20260601T081500Z\r\n"
    "RRULE:FREQ=WEEKLY;COUNT=10\r\n"
    "END:VEVENT\r\n";
  Parser p2;
  Event r[2];
  int rn = feedAll(p2, rec, r, 2);
  CHECK(rn == 1);
  CHECK(strcmp(r[0].rrule, "FREQ=WEEKLY;COUNT=10") == 0);
  uint32_t s0 = (uint32_t)(detail::daysFromCivil(2026, 6, 1) * 86400L) + 8 * 3600;
  CHECK(r[0].start == s0);
  // Ten-week window → exactly 10 occurrences (COUNT limit).
  int occ = 0;
  expand(r[0], s0, s0 + 20u * 7 * 86400, [&](uint32_t, uint32_t) { occ++; });
  CHECK(occ == 10);
  // Narrower window (3 weeks from start) → 3 occurrences.
  occ = 0;
  expand(r[0], s0, s0 + 3u * 7 * 86400, [&](uint32_t, uint32_t) { occ++; });
  CHECK(occ == 3);

  // RRULE field extraction.
  char v[16];
  CHECK(rruleField("FREQ=DAILY;INTERVAL=2;COUNT=5", "INTERVAL", v, sizeof(v)));
  CHECK(strcmp(v, "2") == 0);
  CHECK(rruleField("FREQ=DAILY;INTERVAL=2;COUNT=5", "FREQ", v, sizeof(v)));
  CHECK(strcmp(v, "DAILY") == 0);
  CHECK(!rruleField("FREQ=DAILY", "UNTIL", v, sizeof(v)));

  // Monatliche Wiederholung: 4 Monate Fenster → 4 Vorkommen.
  const char* mon =
    "BEGIN:VEVENT\r\nSUMMARY:Miete\r\nDTSTART:20260101T000000Z\r\n"
    "RRULE:FREQ=MONTHLY\r\nEND:VEVENT\r\n";
  Parser p3; Event m[2];
  CHECK(feedAll(p3, mon, m, 2) == 1);
  uint32_t ms = (uint32_t)(detail::daysFromCivil(2026, 1, 1) * 86400L);
  occ = 0;
  expand(m[0], ms, (uint32_t)(detail::daysFromCivil(2026, 5, 1) * 86400L), [&](uint32_t, uint32_t) { occ++; });
  CHECK(occ == 4);   // Jan, Feb, Mar, Apr

  printf("  ical ok\n");
  return 0;
}

int main() {
  if (testMathquiz()) return 1;
  if (testFlashcards()) return 1;
  if (testPodcast()) return 1;
  if (testReinschrift()) return 1;
  if (testIcal()) return 1;
  printf("ALL TESTS PASSED (%d checks)\n", s_checks);
  return 0;
}

# Flashcard decks

Example decks for the **Flashcards app** (the launcher tile labeled “Learn”).
The app reads decks from the SD card’s **`/flashcards`** directory.

## Transfer decks to the device

Upload files from this directory to the SD card’s `/flashcards` directory
using the **Web file manager** (open the “Files” app, start Wi-Fi, then visit
`http://fennek.local`). The app creates the directory automatically the first
time it is opened.

## Deck format

Each text file (`.txt` or `.tsv`) is one deck. The filename without its
extension is the deck title shown in the list. Each line contains one card:

```
Front<TAB>Back
```

- Use a **tab** as the separator (preferred), or ` | ` (pipe) as a fallback.
- Blank lines and lines starting with `#` are treated as comments.
- Each side can contain about 95 characters; longer text is truncated.
- UTF-8, including accented characters, is supported; display output uses
  `gui::print`.

## Leitner learning system

Each card moves through five boxes. “Correct” advances a card by one box,
increasing its review interval (1 / 3 / 7 / 16 days); “Incorrect” sends it back
to box 0. A **practice session contains at most 10 cards**: the app collects
cards that are due, shuffles them, then prioritizes cards in lower boxes (less
learned cards appear first; learned cards appear less often). Incorrectly
answered cards are repeated in the same session. Progress is stored per deck
at `/flashcards/.progress/<deck>.prg`; cards are referenced by a CRC of the
front side, so progress survives deck reordering or additions.

> The device has no hardware clock. Until system time has been set (by GPS,
> NTP, or mesh since cold boot), all cards are considered due. Scheduling starts
> once the clock is set.

## Included decks

- `schwedisch.txt` — a complete Swedish deck (~700 cards) covering basic
  vocabulary, numbers and time, daily life, travel, verbs, adjectives, useful
  phrases, and conversation, all mixed in one deck.

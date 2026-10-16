// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// i18n.h — English-language UI string catalog.
//
// All UI strings use tr(Str::...). The X-macro list keeps each ID and string
// together, avoiding positional drift between the enum and catalog.
//
// Formatting rules:
//  - Fmt* entries are snprintf format strings; keep each conversion
//    specifier's count, order, and type aligned with its call site.
//  - At textSize 2 (12 px per character), button and tile labels have limited
//    widths; keep short labels concise.
//  - Do not translate NVS keys, paths, glyph escapes, LoRa terminology, or
//    "Fennek".
// =============================================================================
#pragma once
#include <stdint.h>

// X(Id, English)
#define FENNEK_STRS(X) \
  /* --- App names (status bar) --- */ \
  X(AppLauncher,   "Home") \
  X(AppMusic,      "Music") \
  X(AppBook,       "Audiobook") \
  X(AppReader,     "Reading") \
  X(AppMesh,       "Mesh") \
  X(AppSettings,   "Settings") \
  X(AppGames,      "Games") \
  X(AppFiles,      "Files") \
  X(AppNotes,      "Notes") \
  X(AppAlarm,      "Alarm") \
  X(AppMaps,       "Maps") \
  X(AppGyro,       "Motion") \
  X(AppMath,       "Mental Math") \
  X(AppCards,      "Flashcards") \
  X(AppPodcast,    "Podcast") \
  X(AppCalendar,   "Calendar") \
  X(AppTodo,       "Todo") \
  /* --- Launcher tiles (8-character maximum) --- */ \
  X(TileMusic,     "Music") \
  X(TileBook,      "Listen") \
  X(TileReader,    "Read") \
  X(TileMesh,      "Mesh") \
  X(TileSettings,  "Settings") \
  X(TileGames,     "Games") \
  X(TileFiles,     "Files") \
  X(TileNotes,     "Notes") \
  X(TileAlarm,     "Alarm") \
  X(TileMaps,      "Maps") \
  X(TileGyro,      "Motion") \
  X(TileMath,      "Math") \
  X(TileCards,     "Learn") \
  X(TilePodcast,   "Podcast") \
  X(TileCalendar,  "Calendar") \
  X(TileTodo,      "Todo") \
  X(TileSoon,      "(soon)") \
  X(FmtResume,     "Resume: %s") /* fmt: %s */ \
  /* --- Common buttons --- */ \
  X(BtnHome,       "Home") \
  X(BtnBack,       "Back") /* 110 px */ \
  X(BtnBackShort,  "Back") /* 76 px, max 6 */ \
  X(BtnUp,         "Up") /* 72 px, max 5 */ \
  X(BtnDown,       "Down") /* 72 px */ \
  X(BtnList,       "List") /* 76 px, max 6 */ \
  X(BtnPlayer,     "Player") \
  X(BtnRetrySD,    "Search again") \
  X(BtnCancel,     "Cancel") \
  X(BtnStart,      "Start") \
  /* --- Common messages --- */ \
  X(NoSdCard,      "No SD card") \
  X(InsertCard,    "Insert card, then:") \
  X(StatusLocked,  "Locked") \
  X(SleepWakeHint, "Press the top button to wake") \
  X(LockUnlockHint,"Press the top button to unlock") \
  X(EmptyList,     "(empty)") \
  /* --- Music --- */ \
  X(MusicArtists,  "Artists") \
  X(MusicAlbum,    "Albums") \
  X(MusicAlbums,   "Albums") \
  X(MusicTitles,   "Songs") \
  X(MusicPlaylist, "Playlist") \
  X(MusicPlaylists,"Playlists") \
  X(MusicPlayback, "Now playing") \
  X(BtnShuffle,    "Mix") /* 70 px, max 5 */ \
  X(RepOff,        "Rep.") /* 70 px */ \
  X(RepOne,        "Rep.1") \
  X(RepAll,        "Rep.A") \
  X(FmtTagScan,    "Tags %d/%d") /* fmt: %d %d */ \
  /* --- Audiobook --- */ \
  X(BookHeader,    "Audiobooks") \
  X(BookNone,      "No audiobooks") \
  X(BookHint1,     "Create folder /audiobooks,") \
  X(BookHint2,     "one subfolder per book.") \
  X(BtnBookList,   "Book list") /* 228 px */ \
  X(FmtChapter,    "Chapter %d/%d  %s") /* fmt: %d %d %s */ \
  /* --- Reader --- */ \
  X(ReaderBooks,   "Books") \
  X(ReaderSearch,  "Scanning books ...") \
  X(ReaderNone,    "No books") \
  X(ReaderHint,    "Put .txt/.epub in /books.") \
  X(FmtReaderFound,"%d found") /* fmt: %d */ \
  X(ReaderRescan,  "R: rescan") \
  /* --- Notizen --- */ \
  X(NotesToday,    "+ Today") \
  X(NotesEmpty,    "No notes yet") \
  X(NotesDelQ,     "Delete? Enter=yes") \
  /* --- Mesh --- */ \
  X(MeshChannel,   "Mesh channel") \
  X(MeshChats,     "Chats") \
  X(MeshContacts,  "Contacts") \
  X(MeshNoMsgs,    "(no messages yet)") \
  X(MeshNoContacts,"No contacts") \
  X(MeshCtHint1,   "No chats yet.") \
  X(MeshCtHint2,   "Contacts button lists all nodes.") \
  X(FmtMeshStatus, "NF%d RX%lu TX%lu  %dCt %s") /* fmt: %d %lu %lu %d %s */ \
  X(MeshResendHint,"Enter: resend !") \
  X(FmtAge,        "%s ago") /* fmt: %s */ \
  X(BtnRetry,      "Retry") /* 72 px, max 6 */ \
  X(MeshNoRadio,   "Radio not found") \
  X(MeshInitFail,  "SX1262 init failed.") \
  X(MeshSeeLog,    "Details in serial log.") \
  X(MeshMe,        "me") \
  X(BtnChannel,    "Chan.") \
  X(BtnContacts,   "Cont.") \
  X(BtnAdvert,     "Advert") \
  X(MeshNewChannel,"\x10 New channel") \
  X(MeshJoinTitle, "New channel") \
  X(MeshJoinHint,  "Type name, Enter to join") \
  X(BtnJoin,       "Join") \
  /* --- Settings (label/value layout; values use LoRa terminology) --- */ \
  X(SettingsManual,"Manual") \
  X(SecRadio,      "Radio — applies instantly") \
  X(SecSystem,     "System") \
  X(LblFreq,       "Frequency") \
  X(LblBandwidth,  "Bandwidth") \
  X(LblCodingRate, "Coding rate") \
  X(LblTxPower,    "TX power") \
  X(LblName,       "Name") \
  X(LblStandby,    "Auto-standby") \
  X(StandbyOff,    "Off") \
  X(LblFontSize,   "Font size") \
  X(FontSmall,     "Small") \
  X(FontLarge,     "Large") \
  X(HintEnterSave, "Enter saves") /* max 19 Z. */ \
  X(HintNameEdit,  "Enter: edit name") /* max 19 Z. */ \
  X(HintEdit,      "Enter: edit") /* max 19 Z. */ \
  X(LblWifiSsid,   "Wi-Fi SSID") \
  X(LblWifiPass,   "Wi-Fi password") \
  X(HintChange,    "A/D or tap changes") /* max 19 Z. */ \
  X(FmtBattery,    "Batt. %u%%%s (%u mV)") /* fmt: %u %s %u, max 20 Z. */ \
  /* --- Games menu --- */ \
  X(Game2048,      "2048") \
  X(GameMines,     "Minesweeper") \
  X(GameChess,     "Chess") \
  X(GameTtt,       "Tic-tac-toe") \
  X(GameSudoku,    "Sudoku") \
  X(NoGameYet,     "No game yet") \
  X(FmtBest,       "Best: %lu") /* fmt: %lu */ \
  /* --- 2048 --- */ \
  X(FmtPoints,     "Score: %lu") /* fmt: %lu */ \
  X(Hint2048,      "WASD moves · N=New · Backspace=Menu") \
  X(Won2048,       "2048 reached!") \
  X(ContinueEnter, "Keep playing: Enter") \
  X(GameOver,      "Game over!") \
  X(Fmt2048Over,   "Score: %lu · N = new game") /* fmt: %lu */ \
  /* --- Minesweeper --- */ \
  X(MinesWin,      "Cleared!") \
  X(MinesBoom,     "Boom!") \
  X(FmtMinesTime,  "%lu s · N = new game") /* fmt: %lu */ \
  X(MinesNewGame,  "N = new game") \
  X(FmtMinesLeft,  "Mines: %d") /* fmt: %d */ \
  X(BtnFlag,       "Flag") /* 102 px, max 8 */ \
  X(BtnDig,        "Dig") /* 102 px */ \
  X(FmtMinesWins,  "Wins: %u  ·  Best: %u s") /* fmt: %u %u */ \
  X(NoWinYet,      "No win yet") \
  /* --- Chess --- */ \
  X(ChessNewGame,  "New game") \
  X(FmtOpponent,   "Opponent: %s") /* fmt: %s */ \
  X(TwoPlayers,    "2 players") \
  X(FmtYourColor,  "Your color: %s") /* fmt: %s */ \
  X(ColorWhite,    "White") \
  X(ColorBlack,    "Black") \
  X(FmtLevel,      "Level: %s") /* fmt: %s */ \
  X(LevelEasy,     "Easy") \
  X(LevelMid,      "Medium") \
  X(LevelHard,     "Hard") \
  X(ChessBackHint, "Backspace = back to game") \
  X(ChessPromo,    "Promotion:") \
  X(ChessMateW,    "Mate - White wins!") \
  X(ChessMateB,    "Mate - Black wins!") \
  X(ChessStale,    "Stalemate - draw") \
  X(ChessDraw50,   "Draw (50 moves)") \
  X(ChessDrawRep,  "Draw (repetition)") \
  X(ChessThinking, "Fennek thinking ...") \
  X(FmtChessTurn,  "%s to move%s") /* fmt: %s %s */ \
  X(ChessCheck,    " - check!") \
  X(ChessNewHint,  "N = new game") \
  X(YouWhite,      "You: White") \
  X(YouBlack,      "You: Black") \
  X(ChessKeyHint,  "WASD+Enter or tap · N=New · Backspace=Menu") \
  X(FmtChessSaved, "Game running · Wins: %u") /* fmt: %u */ \
  X(FmtChessWins,  "Wins vs Fennek: %u") /* fmt: %u */ \
  /* --- Chess coach --- */ \
  X(ChessTrainer,  "Coach") \
  X(ChessAnalyzing,"Coach reviewing ...") \
  X(ChessClsBest,  "Best move") \
  X(ChessClsGood,  "Good") \
  X(ChessClsInacc, "Inaccuracy") \
  X(ChessClsMist,  "Mistake") \
  X(ChessClsBlund, "Blunder") \
  X(ChessBetter,   "Better:") \
  X(ChessRsnMate,  "Leads to mate.") \
  X(ChessRsnMissM, "There was a mate!") \
  X(ChessRsnHang,  "Piece left hanging.") \
  X(ChessRsnMissC, "Missed a capture.") \
  X(ChessRsnGoodC, "Good capture.") \
  X(ChessRsnCheck, "Active check.") \
  X(ChessRsnSolid, "Solid move.") \
  X(ChessRsnBetter,"A clearly better move existed.") \
  X(ChessTrainerHint,"Coach rates each of your moves") \
  /* --- Tic-tac-toe --- */ \
  X(TttXWins,      "X wins!") \
  X(TttOWins,      "O wins!") \
  X(TttYouWin,     "You win!") \
  X(TttFennekWins, "Fennek wins!") \
  X(TttDraw,       "Draw!") \
  X(FmtTttTurn,    "%s's turn") /* fmt: %s */ \
  X(TttYouAreX,    "You are X") \
  X(TttVsFennek,   "Opponent: Fennek") \
  X(TttTwoPlayers, "2 players on device") \
  X(TttKeyHint,    "N=New  M=Mode  Backspace=Menu") \
  X(FmtTttWins,    "Wins: %u  ·  Draws: %u") /* fmt: %u %u */ \
  /* --- Files (web file manager) --- */ \
  X(FilesTitle,    "Web file manager") \
  X(FilesNoCreds1, "Wi-Fi is not configured.") \
  X(FilesNoCreds2, "Set it in the settings:") \
  X(FilesNoCreds3, "or in the serial monitor (USB):") \
  X(FmtFilesSsid,  "Wi-Fi: %s") /* fmt: %s */ \
  X(FilesStartHint,"Stops music, pauses mesh") \
  X(FilesConnecting,"Connecting ...") \
  X(FilesFailed,   "Connection failed") \
  X(FilesRetryHint,"Enter = try again") \
  X(FilesBrowserHint,"Open in your browser:") \
  X(FmtRequests,   "Requests: %lu") /* fmt: %lu */ \
  X(FilesStopHint, "Q or Back = stop")

namespace i18n {

enum class Str : uint16_t {
  None = 0,
#define X(id, en) id,
  FENNEK_STRS(X)
#undef X
  COUNT
};
constexpr uint16_t STR_COUNT = (uint16_t)Str::COUNT;

const char* tr(Str id);

}  // namespace i18n

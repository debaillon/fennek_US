// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

#include "i18n.h"

namespace {

const char* const kEnglish[] = {
  "",
#define X(id, en) en,
  FENNEK_STRS(X)
#undef X
};

static_assert(sizeof(kEnglish) / sizeof(kEnglish[0]) == i18n::STR_COUNT,
              "English string catalog is incomplete");

}  // namespace

namespace i18n {

const char* tr(Str id) {
  const uint16_t index = (uint16_t)id;
  return index < STR_COUNT ? kEnglish[index] : "";
}

}  // namespace i18n

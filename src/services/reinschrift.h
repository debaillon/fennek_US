// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// reinschrift.h — Sync todos with the Reinschrift Markdown database through
// Nextcloud/WebDAV (desktop/web counterpart: ../TodosExtension).
//
// Background (CLAUDE.md): Wi-Fi and audio cannot run together on this hardware.
// Sync therefore does NOT run continuously; it runs on demand (app/console) and
// opportunistically before automatic standby, like scrobble/notes_ai/podcast.
//
// CONFLICT RESOLUTION is central (multiple devices edit the same file).
// Never blindly overwrite the remote file:
//   1. GET the latest file + ETag.
//   2. reinschrift_core::applyOps() applies ONLY our local changes (an
//      operation queue addressed by ^marker) to the latest version.
//   3. PUT with If-Match:<etag>. If 412 (another client was faster), repeat
//      GET+merge once. This preserves other clients' edits to other tasks.
// The operation queue is persisted on SD (/.fennek/todo_ops.tsv) and survives
// deep sleep.
//
// The app reads the parsed task list (count()/get()) and makes mutations
// (toggle/setDueRelative/add). They take effect locally immediately and enqueue
// an operation; the next sync() uploads it.
// =============================================================================
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "apps/reinschrift_core.h"

namespace reinschrift_svc {

// Load the cache (task list + ETag) and operation queue from SD; build the list.
void begin();

// Number/access of currently known tasks (including pending local changes so
// the app can display them immediately).
int  count();
bool get(int idx, reinschrift::Task* out);

// Mutations (idx refers to the order returned by get()). Enqueue an operation,
// update the local view, and persist the queue.
void toggle(int idx);                       // Toggle completion
void setDueRelative(int idx, int dayOffset);// 0 = today, 1 = tomorrow (local noon)
bool add(const char* title, const char* topic);  // Add a task (quick add)

// Number of changes not yet uploaded.
int pendingCount();

// Blocking sync (manages Wi-Fi itself). log receives a status line.
// useBackoff is only for the pre-standby path.
bool sync(char* log, size_t logN);

// Before automatic standby (with RTC RAM back-off). true = nothing to do / success.
bool flushBeforeStandby();

}  // namespace reinschrift_svc

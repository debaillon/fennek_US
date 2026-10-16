// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// settings_app.h — Einstellungen als App.
//
// Radio parameters (EU Narrow / EU Classic / US 915 presets or individually
// adjustable frequency, bandwidth, SF, CR, and transmit power), node name, and
// device info. Changes are saved to NVS immediately and applied live if mesh is running
// (mesh_client::applyRadioParams()).
// =============================================================================
#pragma once

#include "core/appmgr.h"

namespace settings_app {

App* get();

}  // namespace settings_app

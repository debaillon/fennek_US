// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// gyro_app.h — sensor/orientation display ("Gyro" tile, second home screen).
//
// Displays the BHI260AP IMU sensor (I2C 0x28). Stage 1 detects sensor presence
// (I2C probe). Full sensor reading requires uploading Bosch BHy2 firmware
// (smart sensor hub; not a raw register read) and will be verified on-device.
// The sensor runs only while this app is in the foreground to save power.
// =============================================================================
#pragma once

#include "core/appmgr.h"

namespace gyro_app {

App* get();

}  // namespace gyro_app

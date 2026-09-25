// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "config.h"
#include "session.h"

namespace tow_ht::mod_hotkeys {

// Register the hotkey lists HeadTracking.ini names, and start the poller. The
// session is what the tracking-mode cycle acts on. The mode cycle and the yaw
// toggle save what they set; the tracking toggle changes the session only.
// False when the poller thread did not start, in which case every binding it
// registered is dead for the session and the caller has to say so rather than
// print a controls line naming keys that do nothing.
bool Register(Session& session, const Config& config);

}  // namespace tow_ht::mod_hotkeys

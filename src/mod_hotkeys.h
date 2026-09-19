// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "config.h"
#include "session.h"

namespace tow_ht::mod_hotkeys {

// Register the nav-cluster keys and their Ctrl+Shift chord alternatives, and
// start the poller. The session is what the tracking-mode cycle acts on; the
// config carries the one rebindable key, [Hotkeys] YawModeKey.
// False when the poller thread did not start, in which case every binding it
// registered is dead for the session and the caller has to say so rather than
// print a controls line naming keys that do nothing.
bool Register(Session& session, const Config& config);

}  // namespace tow_ht::mod_hotkeys

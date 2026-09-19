// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/tracking/head_tracking_session.h"

namespace tow_ht {

using Session = cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver>;

// Without this the session would silently report every tracker as local and pin
// smoothing to LocalSmoothing forever, with no compile error.
static_assert(Session::kHasRemoteConnection,
              "receiver must expose IsRemoteConnection() for per-connection smoothing");

}  // namespace tow_ht

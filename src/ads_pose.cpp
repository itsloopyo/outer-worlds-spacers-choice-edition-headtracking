// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "ads_pose.h"

namespace tow_ht::ads_pose {

namespace {

// Game thread only, with the frame walk that drives it.
cameraunlock::ads::AdsFade g_fade;

}  // namespace

Result Advance(const TrackingState& state, const HeadPose& absolute,
               unsigned long long nowMs) {
    Result out;
    out.LeanScale = g_fade.Update(state.aiming, nowMs);
    out.Pose = EaseLean(absolute, out.LeanScale);
    return out;
}

void Reset() { g_fade.Reset(); }

}  // namespace tow_ht::ads_pose

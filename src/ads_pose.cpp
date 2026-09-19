// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "ads_pose.h"

#include "logging.h"

namespace tow_ht::ads_pose {

namespace {

// Both live on the game thread with the frame walk that drives them.
AdsFade g_fade;
AdsEntryPose g_entry;

// One line the first time the sights come up in each mode, naming what that mode
// does. This is what a player's log shows when they report that aiming does
// something they did not expect.
//
// Not once per aim. Fine aim is held for most of a firefight and dropped between
// every shot, so a line each way on every edge would bury the build check and
// the hook lines the log exists for.
void LogFirstAdsEntry(bool aiming, AdsMode mode) {
    if (!aiming) return;
    static unsigned s_logged = 0;
    const unsigned bit = 1u << static_cast<unsigned>(mode);
    if (s_logged & bit) return;
    s_logged |= bit;
    switch (mode) {
        case AdsMode::Paused:
            Log::Line("ads: sights up - head tracking paused, view settling onto the aim");
            break;
        case AdsMode::Tracked:
            Log::Line("ads: sights up - view settling onto the aim, head tracking carries "
                      "on from there, using the game's reticle");
            break;
    }
}

}  // namespace

Result Advance(const TrackingState& state, const AdsEntryPose::Pose& absolute,
               unsigned long long nowMs) {
    const AdsMode mode = GetAdsMode();

    // Raising the sights hands the view back to the gun: the head pose eases out
    // over a fraction of a second and the frame settles onto the aim, which is
    // where the shot was already going. Both modes make that same swing and
    // differ in where the fade lands - nothing in `paused`, the entry-relative
    // pose in tracked. Roll is in neither fade; see BlendAdsPose.
    //
    // Both are asked in every mode, so the entry pose is dropped when the weapon
    // comes down whichever mode was live while it was up, and a mode cycled
    // mid-aim takes effect on that aim.
    LogFirstAdsEntry(state.aiming, mode);

    Result out;
    out.Scale = g_fade.Update(state.aiming, nowMs);
    const AdsEntryPose::Pose relative = g_entry.Relative(state.aiming, true, absolute);
    out.Pose = BlendAdsPose(mode, out.Scale, absolute, relative);
    return out;
}

void Reset() {
    g_fade.Reset();
    g_entry.Reset();
}

}  // namespace tow_ht::ads_pose

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "ads.h"
#include "game_state.h"

// Whether the head pose reaches the view this frame, and why not when it does
// not.
//
// Kept out of the render hook as a pure function so the walk can be exercised
// without the game. Every one of its answers is a frame the player either sees
// their head in or does not.
//
// game_state.h owns the one gate that reads the live process - the cursor flag
// that separates gameplay from the inventory, the pause menu and the cinematics
// either side of a map transition, with dialogue told apart from those. This
// file is the order the gates are asked in, plus the master toggle, the tracker
// and the sights. The Outer Worlds ships no multiplayer, so there is no net gate
// to sit above them.
namespace tow_ht {

enum class TrackingVerdict {
    // The head pose is applied in full.
    Active,
    // The sights are up in `paused` mode. The pose is still fed to the camera,
    // because it is being EASED off rather than switched off - see AdsFade - and
    // once it has gone the frame is the frame the game would have drawn on its
    // own, bar the head tilt: roll is left out of the fade in every mode, since
    // it moves neither the eye off the barrel nor the aim off the middle of the
    // frame (cameraunlock/ads/ads_blend.h).
    AdsSuspended,
    // The master toggle is off.
    Disabled,
    // A conversation. The pose is applied without the sights logic, since
    // nothing is being aimed, and zoom compensated as in gameplay, so a close-up
    // does not magnify the head turn.
    Conversation,
    // A menu, the inventory, a loading screen or a cinematic. An unreadable
    // cursor flag lands here too: game_state fails closed, and an unreadable
    // frame must never be the one that turns tracking on.
    NotGameplay,
    // Tracking is on and the tracker has published nothing this frame.
    NoTracker,
};

struct TrackingState {
    TrackingVerdict verdict = TrackingVerdict::NotGameplay;
    // The sights are up. Reported in EVERY mode, including `paused` where the
    // gate is closed: the gate says whether tracking applies, this says what the
    // weapon is doing so the pose transition can follow it.
    bool aiming = false;
};

// ADS is tested LAST, so a menu, a loading screen or a dead tracker still names
// its own reason when both are true at once - and every earlier return leaves
// `aiming` false so a menu cannot keep an ADS transition active.
inline TrackingState DecideTracking(const game_state::Verdict& gate,
                                    bool trackingEnabled, bool havePose,
                                    bool aiming, AdsMode mode) {
    TrackingState s;
    if (!trackingEnabled) {
        s.verdict = TrackingVerdict::Disabled;
        return s;
    }
    if (!gate.InGameplay) {
        if (!gate.InConversation)
            s.verdict = TrackingVerdict::NotGameplay;
        else
            s.verdict = havePose ? TrackingVerdict::Conversation : TrackingVerdict::NoTracker;
        return s;
    }
    if (!havePose) {
        s.verdict = TrackingVerdict::NoTracker;
        return s;
    }
    s.aiming = aiming;
    s.verdict = (aiming && AdsSuspendsTracking(mode)) ? TrackingVerdict::AdsSuspended
                                                      : TrackingVerdict::Active;
    return s;
}

// A pose is fed to the camera in both of the first two verdicts. AdsSuspended
// needs it because suspending is an ease-out, not a switch: dropping the pose on
// the falling edge into ADS would throw away the smoothing state, and lowering
// the weapon would then swing the view back through the whole head angle.
inline bool PoseApplies(TrackingVerdict verdict) {
    return verdict == TrackingVerdict::Active || verdict == TrackingVerdict::AdsSuspended;
}

// One line for the log and the heartbeat. Never null.
inline const char* Reason(TrackingVerdict verdict) {
    switch (verdict) {
        case TrackingVerdict::Active:       return "gameplay";
        case TrackingVerdict::AdsSuspended: return "sights up (ADS mode: paused)";
        case TrackingVerdict::Disabled:     return "tracking toggled off";
        case TrackingVerdict::Conversation: return "conversation";
        case TrackingVerdict::NotGameplay:  return "menu or loading";
        case TrackingVerdict::NoTracker:    return "no tracker data";
    }
    return "unknown";
}

}  // namespace tow_ht

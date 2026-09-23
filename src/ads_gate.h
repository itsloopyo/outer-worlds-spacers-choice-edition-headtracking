// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

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
    // The head pose is applied in full, sights up or not.
    Active,
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
    // The sights are up. It never closes the gate: it only tells the lean
    // easing what the weapon is doing (ads_pose.h).
    bool aiming = false;
};

// ADS is tested LAST, so a menu, a loading screen or a dead tracker still names
// its own reason when both are true at once - and every earlier return leaves
// `aiming` false so a menu cannot keep the lean eased out.
inline TrackingState DecideTracking(const game_state::Verdict& gate,
                                    bool trackingEnabled, bool havePose,
                                    bool aiming) {
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
    s.verdict = TrackingVerdict::Active;
    return s;
}

inline bool PoseApplies(TrackingVerdict verdict) {
    return verdict == TrackingVerdict::Active;
}

// One line for the log and the heartbeat. Never null.
inline const char* Reason(TrackingVerdict verdict) {
    switch (verdict) {
        case TrackingVerdict::Active:       return "gameplay";
        case TrackingVerdict::Disabled:     return "tracking toggled off";
        case TrackingVerdict::Conversation: return "conversation";
        case TrackingVerdict::NotGameplay:  return "menu or loading";
        case TrackingVerdict::NoTracker:    return "no tracker data";
    }
    return "unknown";
}

}  // namespace tow_ht

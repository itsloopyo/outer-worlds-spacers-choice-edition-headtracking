// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

// When head tracking is allowed to touch the view at all.
//
// The Outer Worlds is single-player throughout - the shipped game has no
// multiplayer mode, no co-op and no online session of any kind, so there is no
// networked case to stand down for and no net gate here. What there is instead
// is a lot of non-gameplay: dialogue, the inventory and character sheets, the
// ship terminal, the pause menu, loading, and the cinematics either side of a
// map transition. In every one of them the game raises the mouse cursor, which
// is the flag this reads. Dialogue is then told apart from the rest by
// conversation_state, because head tracking carries on through a conversation.
namespace tow_ht::game_state {

struct Verdict {
    bool InGameplay = false;
    // False until the cursor flag has actually been read once. Kept separate so
    // the log can tell "the player is in a menu" from "the gate is not
    // readable", which are the same decision but not the same bug.
    bool GateKnown = false;
    // The cursor is up for a conversation rather than a menu. Only ever true
    // with InGameplay false.
    bool InConversation = false;
};

// Re-evaluate for this frame. `controller` is the APlayerController the
// GetPlayerViewPoint hook was called on. One guarded load, plus the
// conversation test while the cursor is up.
Verdict Evaluate(std::uintptr_t controller);

// Log a transition, so a session log shows exactly when tracking stood down
// without the heartbeat having to land inside the window.
void LogTransitions(const Verdict& v);

}  // namespace tow_ht::game_state

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "config.h"
#include "session.h"

// The GetPlayerViewPoint hook: the one place the head pose reaches the engine.
//
// Every consumer of the player's view point goes through this function - the
// renderer, the audio listener, the level streamer, the weapon trace, the
// interaction ray. The hook lets exactly one of them, the render/projection
// caller, see the head-tracked pose and hands every other one the clean
// mouse/pad view. That gate IS the look/aim decoupling: the round goes where
// the mouse pointed, and the reticle is moved to meet it rather than the other
// way round.
namespace tow_ht::view_hook {

struct Dependencies {
    Session* session = nullptr;
    const Config* config = nullptr;
};

// Install the trampoline on the active build profile's GetPlayerViewPoint.
// False, having logged why, if MinHook refuses.
bool Install(const Dependencies& deps);

// Master toggle, yaw mode and inject mode, driven by the hotkeys.
void SetTrackingEnabled(bool enabled);
bool TrackingEnabled();
void SetWorldSpaceYaw(bool worldSpace);
bool WorldSpaceYaw();
void SetInjectMode(int mode);
int  InjectMode();

}  // namespace tow_ht::view_hook

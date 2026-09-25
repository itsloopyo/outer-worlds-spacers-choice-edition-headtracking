// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Which GetPlayerViewPoint caller the head pose is written back for.
//
// GetPlayerViewPoint is asked for the view by the renderer, the audio listener,
// the level streamer and the interaction trace alike. Injecting for one caller
// and no other IS the look/aim decoupling, so the mode is a caller index rather
// than an on/off switch.
namespace tow_ht::inject {

// How many caller RVAs a build profile can carry.
constexpr std::size_t kCallerSlots = 16;

using CallerRvas = std::array<std::uintptr_t, kCallerSlots>;

// Every caller gets the pose. Diagnostic only - it entangles aim with view -
// and it is the mode that logs the per-return-address caller summary used to
// (re-)identify the render path after a patch.
constexpr int kAllCallers = 0;

// Inject only for kKnownCallerRvas[0]; mode n covers slot n-1.
constexpr int kFirstCaller = 1;

// No caller gets the pose.
constexpr int kNone = static_cast<int>(kCallerSlots) + 1;

constexpr int kModeCount = kNone + 1;

}  // namespace tow_ht::inject

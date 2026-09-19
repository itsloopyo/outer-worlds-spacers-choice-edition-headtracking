// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include "ue4_types.h"

// Where the shot stops, this frame.
//
// The reticle marks a POINT, not a direction. With the eye on the shot's own
// ray the two project to the same pixel, which is why a direction is enough
// until 6DOF lands; a lean up to 30cm off that ray separates them, and a
// reticle built on a direction then slides off the thing it marks, worse the
// closer the target. So the parallax term needs the live distance to whatever
// the player is pointing at, and it has to be THIS frame's distance: a fixed,
// smoothed or stale depth leaves an error proportional to
// lean * (1/d0 - 1/d), which is zero at exactly one range and changes sign
// either side of it.
//
// The cast runs through the engine's own UKismetSystemLibrary::LineTraceSingle
// so there is no second physics query implementation in this mod to disagree
// with the game's, and its parameter frame is read out of the engine's
// reflection data rather than assumed - see ue_reflect.h.
namespace tow_ht::aim_trace {

struct Result {
    // False when the trace could not be performed at all: the reflection data
    // did not resolve, the VM is not up, or the dispatch faulted. The caller
    // must invalidate the reticle rather than reuse an older point.
    bool Valid = false;
    // True when something blocked the ray. False is a definite no-hit, which is
    // a target at infinity: the caller projects the aim DIRECTION for that
    // frame rather than substituting a magic distance.
    bool Hit = false;
    UeVector Point{0.0f, 0.0f, 0.0f};
    float Distance = 0.0f;   // along `dir` from `start`, in UE units (cm)
};

// Resolve the UFunction, its parameter frame and FHitResult's fields. Returns
// false, having logged exactly which lookup failed, when the layout does not
// resolve; the caller then marks the aim direction while the head is centred and
// marks nothing once a lean separates the render eye from the shot's own ray.
// Safe to call every frame - it does the work once and then answers from cache.
bool Ready();

// Whether the last Ready() attempt failed permanently, for the heartbeat.
bool Failed();

// Whether the cast resolved but is now faulting on every dispatch, for the
// heartbeat. Distinct from Failed(), which is a resolve that never got started.
bool DispatchFailing();

// ETraceTypeQuery index the cast uses. Configurable because which query channel
// a game's bullets stop on is a project setting, not an engine constant, and
// the only way to know is to fire at a wall and compare.
void SetTraceChannel(int channel);

// Cast from `start` along `dir` (unit vector) for `maxDistance` UE units.
// `pawn` is the actor to exclude, so the ray does not stop on the player's own
// body a centimetre in front of the eye. Must be called on the game thread.
Result Cast(std::uintptr_t pawn, const UeVector& start, const UeVector& dir,
            float maxDistance);

}  // namespace tow_ht::aim_trace

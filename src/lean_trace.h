// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include <cameraunlock/camera/lean_clamp.h>
#include <cameraunlock/math/vec3.h>

// The engine half of the lean collision clamp: ask the game how far the eye may
// travel from where it put the camera before it meets something solid.
//
// Core owns what to do with the answer (cameraunlock/camera/lean_clamp.h); this
// owns getting one. The sweep goes through the engine's own
// UKismetSystemLibrary::SphereTraceSingle, dispatched by name through the script
// VM with its parameter frame read out of the engine's reflection data - the
// same route aim_trace takes - so there is no second physics query in this mod
// to disagree with the game's, and no RVA to re-pin every patch.
//
// A SPHERE sweep rather than a line: the radius IS the standoff, so the hit
// location comes back already backed off the surface, and a sphere cannot slip
// through a gap a line would thread and the near plane would then see through.
namespace tow_ht::lean_trace {

// Radius of the swept sphere, in UE units (cm). This IS the standoff from the
// surface, so it must exceed the camera's near clip distance or geometry is
// culled before the eye reaches it and the wall goes transparent anyway.
void SetRadius(float centimetres);

// Which ETraceTypeQuery the sweep runs on. Configurable because the channel a
// level's geometry blocks is a project setting, not an engine constant, and the
// only way to know is to run it and read the log.
void SetChannel(int traceTypeQuery);

// Resolve the sweep and FHitResult against the live reflection data. Safe to
// call every frame - it does the work once and answers from cache.
bool Ready();

// True when the resolve failed for the session, for the heartbeat.
bool Failed();

// The pawn to sweep from, set once per frame by the view hook.
//
// The pawn rather than the controller, and it matters: it is passed as the
// sweep's WorldContextObject with bIgnoreSelf set, which is what excludes the
// player's own capsule. The eye sits inside that capsule, so without it every
// sweep starts penetrating and reports nowhere to go - the lean would simply
// stop working, everywhere, with no obvious cause.
void SetPawn(std::uintptr_t pawn);

// The query, in the shape core's clamp takes. `context` is unused - the pawn
// comes from SetPawn - and is present to match LeanQueryFn. Game thread only.
cameraunlock::camera::LeanObstruction Query(void* context,
                                            const cameraunlock::math::Vec3& start,
                                            const cameraunlock::math::Vec3& direction,
                                            float maxDistance);

}  // namespace tow_ht::lean_trace

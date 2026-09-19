// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cameraunlock/unreal/ue_math.h>

#include "ue4_types.h"

// Where the shot lands, in the picture the head is looking at.
//
// The head turns the view off the aim and a lean moves the eye off the shot's
// own ray, so screen centre stops being where the rounds go the moment tracking
// is live. This is the one place that works out where they do go on screen; the
// reticle mover pushes the answer onto the game's own crosshair. Two
// projections drift apart and only one of them can be right, so there must
// never be a second.
//
// The projection takes a POINT, not a direction, and that is the whole design.
// A direction is a point at infinity: correct at every range while the eye that
// draws sits on the shot's ray, and wrong by lean/depth as soon as it does not.
// A fixed, smoothed or stale depth is worse still - it leaves an error
// proportional to lean * (1/d0 - 1/d), zero at exactly one range and changing
// sign either side of it, which is the failure the reticle doctrine says costs
// days to find. So the aim point comes from a live per-frame cast (aim_trace.h)
// and a frame whose cast fails marks nothing.
namespace tow_ht::AimProjection {

// Everything the projection needs, all from the frame being drawn, pushed once
// per render-caller frame.
//
//   trackedEye      the camera position the frame is rendered from, i.e. the
//                   clean eye plus the head lean.
//   trackedRotation the rotation the frame is rendered with.
//   cleanAimDir     the clean (mouse/pad) aim direction, unit, world space.
//   leanApplied     the frame moved the eye off the clean one. When it did not,
//                   the render eye IS the shot eye and the clean direction
//                   projects exactly, whatever the cast did or did not do.
//   castRan         a cast was performed this frame and answered. False marks
//                   nothing at all: the reflection layout is unproven, the VM is
//                   not up, or the dispatch faulted, and none of those is a
//                   statement about where the shot lands - but it only costs a
//                   mark when a lean has separated the two eyes.
//   hit             true when that cast stopped on something. A definite no-hit
//                   is a target at infinity, for which the clean direction is
//                   exact, so it projects rather than invalidating.
//   hitPoint        where it stopped. Ignored unless castRan and hit.
//   active          false invalidates the offset entirely - tracking off, no
//                   tracker data, or not in gameplay.
void Update(const UeVector& trackedEye,
            const cameraunlock::unreal::FQuat4d& trackedRotation,
            const UeVector& cleanAimDir,
            bool leanApplied, bool castRan, bool hit, const UeVector& hitPoint,
            bool active);

// Mark nothing until the next Update. For a frame the mod is not allowed to move
// the view - a menu, a loading screen, the master toggle, a dead tracker - where
// leaving the last gameplay frame's offset in place would hold the crosshair off
// centre while the head is doing nothing.
void Invalidate();

// Push the live field of view (degrees) the frame is being drawn with, from
// camera_fov::Read. There is no seeded default: until a real value arrives, and
// on any frame the read fails, the projection reports invalid rather than
// marking a place from a guessed field of view.
void SetFovDegrees(float fovDegrees);

// The aim point as a pixel offset from the centre of the game window's client
// area (+x right, +y down), for the widget mover.
bool GetScreenOffset(float& dx, float& dy);

// Whether the last frame this file PROJECTED marked a real impact point rather
// than the aim direction, and how far away it was. For the diagnostic line that
// says the parallax term is live - which is why the caller must print them only
// beside a valid offset: after an invalid frame they describe the last answer,
// not the current one.
bool LastHadImpactPoint();

// Whether the last frame had a cast to project at all, for the reticle's own
// diagnostic line: a cast that could not run needs a different fix from one that
// ran and hit nothing.
bool LastCastRan();
float LastImpactDistance();

}  // namespace tow_ht::AimProjection

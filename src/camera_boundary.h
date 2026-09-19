// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cameraunlock/unreal/ue_math.h>

// Where the tracker's convention meets Unreal's.
//
// Both conversions below are pure functions of their arguments - no globals, no
// engine reads - so the numbers that took an in-game session to settle can be
// diffed against a sibling mod's. Everything that talks to the live process
// lives above this, in the view hook.
//
// This file is deliberately identical to
// still-wakes-the-deep-headtracking/src/camera_boundary.cpp apart from the
// namespace: same engine family, same GetPlayerViewPoint hook, so the signs
// were settled once in a running game and are inherited rather than re-derived.
namespace tow_ht::camera_boundary {

// Compose the head pose (degrees, tracker convention) onto a clean engine
// rotation, in place.
//
// worldSpaceYaw true adds the pose to the FRotator directly, so yaw turns about
// the world up-axis and the horizon stays level on a pitched turn. False
// post-multiplies a camera-local quaternion, which leans the horizon instead.
// Roll is negated either way: OpenTrack calls the other direction positive.
void ApplyHeadPose(cameraunlock::unreal::FRotator& rotation,
                   double yaw, double pitch, double roll, bool worldSpaceYaw);

// Build a world-space camera-location offset (UE units = cm) from the session's
// processed offset (metres) in the CLEAN camera frame, so head sway follows the
// body rather than the head-rotated view.
cameraunlock::unreal::FVector PositionOffset(
    const cameraunlock::unreal::FQuat4d& cleanRotation,
    float offsetX, float offsetY, float offsetZ);

}  // namespace tow_ht::camera_boundary

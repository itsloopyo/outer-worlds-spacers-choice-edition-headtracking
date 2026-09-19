// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "ads.h"
#include "ads_gate.h"

// What the sights do to the head pose for one frame.
//
// The view hook hands over this frame's absolute pose at the engine boundary and
// gets back the pose to apply. Everything the ADS cycle changes about the camera
// happens here: the transition, the entry-relative pose the tracked modes feed,
// and the one axis neither of them touches.
//
// The transition and the entry pose are core's (cameraunlock/ads/), so this file
// owns only the order they are asked in and the state that has to survive from
// one frame to the next.
//
// The pose handed over is the tracker pose in engine degrees and engine position
// units, BEFORE the zoom compensation. That order matters and it is the reason
// the hook scales afterwards: the entry pose is captured at whatever field of
// view the sights came up from, and scaling first would measure the aim against
// an entry that was recorded at a different magnification.
namespace tow_ht::ads_pose {

// The pose for this frame, and what it was decided from.
struct Result {
    AdsEntryPose::Pose Pose;
    // 1 at the hip, 0 with the sights fully up. Exposed because `paused` has to
    // keep feeding the camera until this reaches 0 - the gate closing is an
    // ease-out, not a switch.
    float Scale = 1.0f;
};

// Advance one frame. `state` is the verdict walk's answer, `absolute` is the
// frame's head pose, and `nowMs` is the caller's clock.
//
// The pose is always a live tracker sample: DecideTracking answers NoTracker
// when it is not, PoseApplies is false for that, and the frame stands down
// before it reaches here. So there is no dead-rotation case for this to guard -
// a dropout arrives as Reset() instead, which drops the entry pose outright.
//
// The mode is read live rather than passed in at startup, so a cycle mid-aim
// lands on the aim already in progress.
Result Advance(const TrackingState& state, const AdsEntryPose::Pose& absolute,
               unsigned long long nowMs);

// Drop the transition and the pose the sights came up on. Called on every frame
// tracking is suppressed - menu, loading, master toggle, tracker dropout - and
// on every conversation frame, so the next aim re-enters from where the head is
// then rather than against a pose from before the interruption.
void Reset();

}  // namespace tow_ht::ads_pose

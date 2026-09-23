// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "ads_gate.h"

#include "cameraunlock/ads/ads_fade.h"

// What the sights do to the head pose for one frame: they ease the lean out, and
// nothing else.
//
// Head tracking carries on through the aim. Rotation turns the rendered camera
// about the eye, and the sight line passes through the eye, so with the head
// turned the sights stay lined up where the weapon points. A lean is different:
// it moves the eye itself off the sight line, and the weapon here is world
// geometry posed from the clean camera, not a pass the mod can redraw from the
// clean eye. So x, y and z scale by core's AdsFade while the sights are up, and
// rotation - roll included - goes through untouched.
namespace tow_ht::ads_pose {

// The tracker pose in engine degrees and engine position units, before the zoom
// compensation.
struct HeadPose {
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct Result {
    HeadPose Pose;
    // The lean scale: 1 at the hip, 0 with the sights fully up.
    float LeanScale = 1.0f;
};

inline HeadPose EaseLean(const HeadPose& absolute, float leanScale) {
    HeadPose out = absolute;
    out.x *= leanScale;
    out.y *= leanScale;
    out.z *= leanScale;
    return out;
}

// Advance one frame. `state` is the verdict walk's answer, `absolute` is the
// frame's head pose, and `nowMs` is the caller's clock.
Result Advance(const TrackingState& state, const HeadPose& absolute,
               unsigned long long nowMs);

// Drop the transition back to the hip. Called on every frame tracking is
// suppressed and on every conversation frame, so the lean does not come back
// eased out after an interruption.
void Reset();

}  // namespace tow_ht::ads_pose

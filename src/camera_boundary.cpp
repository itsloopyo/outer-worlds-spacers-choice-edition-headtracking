// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "camera_boundary.h"

namespace tow_ht::camera_boundary {

namespace {
namespace ue = ::cameraunlock::unreal;

// UE works in centimetres; the processor hands out metres.
constexpr double kMetresToUE = 100.0;
}  // namespace

void ApplyHeadPose(ue::FRotator& rotation,
                   double yaw, double pitch, double roll, bool worldSpaceYaw) {
    if (worldSpaceYaw) {
        rotation.Yaw   += yaw;
        rotation.Pitch += pitch;
        rotation.Roll  -= roll;
        return;
    }
    const ue::FQuat4d baseQ = ue::QuatFromEulerDeg(rotation.Pitch, rotation.Yaw, rotation.Roll);
    const ue::FQuat4d headQ = ue::QuatFromEulerDeg(pitch, yaw, -roll);
    const ue::FRotator composed = ue::QuatToRotator(ue::QuatMul(baseQ, headQ));
    rotation.Pitch = composed.Pitch;
    rotation.Yaw   = composed.Yaw;
    rotation.Roll  = composed.Roll;
}

ue::FVector PositionOffset(const ue::FQuat4d& cleanRotation,
                           float offsetX, float offsetY, float offsetZ) {
    const ue::FVector camFwd   = ue::QuatRotateVec(cleanRotation, ue::FVector{1.0, 0.0, 0.0});
    const ue::FVector camRight = ue::QuatRotateVec(cleanRotation, ue::FVector{0.0, 1.0, 0.0});
    const ue::FVector camUp    = ue::QuatRotateVec(cleanRotation, ue::FVector{0.0, 0.0, 1.0});

    // The processor's z runs the other way to UE's camera-forward: it clamps to
    // [-limit_z, +limit_z_back], i.e. NEGATIVE z is the forward lean. Negate it
    // here, at the engine boundary, rather than via the processor's invert_z -
    // inversion happens before the clamp, so flipping it there would put the
    // generous 0.40m limit on leaning back and 0.10m on leaning in.
    //
    // Sway is negated for a reason that needs no knowledge of what the tracker
    // calls positive: turning the head rotates the face about the neck, so the
    // tracked point slides sideways by sin(yaw)*pivot, and that slide has to go
    // the same way as the turn. In 13 of 13 logged samples with |yaw| > 3 deg
    // the reported x ran OPPOSITE to the reported yaw, at an implied pivot arm
    // of 7-11 cm (a real neck). Yaw maps straight through (three shipped UE
    // siblings agree), so x is the mirrored one; left un-negated the camera
    // slides right as the view turns left.
    const double surge = -static_cast<double>(offsetZ) * kMetresToUE;  // -> forward
    const double sway  = -static_cast<double>(offsetX) * kMetresToUE;  // -> right
    const double heave =  static_cast<double>(offsetY) * kMetresToUE;  // -> up
    return ue::FVector{
        camFwd.X * surge + camRight.X * sway + camUp.X * heave,
        camFwd.Y * surge + camRight.Y * sway + camUp.Y * heave,
        camFwd.Z * surge + camRight.Z * sway + camUp.Z * heave,
    };
}

}  // namespace tow_ht::camera_boundary

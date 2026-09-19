// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Where the tracker's convention meets Unreal's: the axis mapping, the signs
// and the unit scale.
//
// These are the numbers a port gets wrong, and getting them wrong does not
// crash - it ships a camera that leans the wrong way. The mod inherits them
// from still-wakes-the-deep-headtracking rather than re-deriving them, so what
// this suite locks is that they still say what that mod's running-game session
// settled on.

#include <cmath>

#include "camera_boundary.h"
#include "test_harness.h"

namespace {

namespace cb = tow_ht::camera_boundary;
using cameraunlock::unreal::FQuat4d;
using cameraunlock::unreal::FRotator;
using cameraunlock::unreal::FVector;
using cameraunlock::unreal::QuatFromEulerDeg;

FRotator Clean(double pitch, double yaw, double roll) {
    return FRotator{pitch, yaw, roll};
}

// World-space yaw adds the pose to the FRotator directly, so each axis lands on
// its own and nothing bleeds into roll however far the mouse has pitched.
void TestWorldSpaceYawAddsPerAxisAndNegatesRoll() {
    FRotator r = Clean(0.0, 0.0, 0.0);
    cb::ApplyHeadPose(r, 20.0, 10.0, 5.0, true);
    CHECK_NEAR_MSG(r.Yaw, 20.0, 1e-9, "head yaw adds to engine yaw unchanged");
    CHECK_NEAR_MSG(r.Pitch, 10.0, 1e-9, "head pitch adds to engine pitch unchanged");
    CHECK_NEAR_MSG(r.Roll, -5.0, 1e-9,
                   "roll is negated: OpenTrack calls the other direction positive");
}

// The horizon-locked claim, stated as a test: with the camera pitched well off
// the horizon, a pure head yaw must leave roll exactly where it was.
void TestWorldSpaceYawKeepsTheHorizonLevelOnAPitchedCamera() {
    FRotator r = Clean(40.0, 90.0, 0.0);
    cb::ApplyHeadPose(r, 25.0, 0.0, 0.0, true);
    CHECK_NEAR(r.Yaw, 115.0, 1e-9);
    CHECK_NEAR(r.Pitch, 40.0, 1e-9);
    CHECK_NEAR_MSG(r.Roll, 0.0, 1e-9, "world-space yaw introduces no roll");
}

// Camera-local yaw post-multiplies, so against an un-pitched camera it is the
// same composition read a different way and lands on the same angles.
void TestCameraLocalYawMatchesWorldSpaceOnAnUnpitchedCamera() {
    FRotator r = Clean(0.0, 0.0, 0.0);
    cb::ApplyHeadPose(r, 20.0, 10.0, 5.0, false);
    CHECK_NEAR(r.Yaw, 20.0, 1e-6);
    CHECK_NEAR(r.Pitch, 10.0, 1e-6);
    CHECK_NEAR(r.Roll, -5.0, 1e-6);
}

// And the documented cost of the other mode: turning the head about the
// camera's own up-axis while the camera is pitched leans the horizon. This is
// what Page Down switches between, so the difference has to be real.
void TestCameraLocalYawLeansTheHorizonOnAPitchedCamera() {
    FRotator local = Clean(40.0, 90.0, 0.0);
    cb::ApplyHeadPose(local, 25.0, 0.0, 0.0, false);
    CHECK_MSG(std::fabs(local.Roll) > 1.0,
              "camera-local yaw on a pitched camera rolls the view");
}

// The engine boundary in one line: metres in, centimetres out, and the three
// axes onto forward/right/up with the signs the doctrine fixes.
void TestPositionOffsetMapsAxesAndConvertsMetresToCentimetres() {
    const FQuat4d identity = QuatFromEulerDeg(0.0, 0.0, 0.0);

    // The processor clamps z to [-limit_z, +limit_z_back], so NEGATIVE z is the
    // forward lean. It has to come out as +X, which is UE's camera-forward.
    const FVector forward = cb::PositionOffset(identity, 0.0f, 0.0f, -0.30f);
    CHECK_NEAR_MSG(forward.X, 30.0, 1e-4,
                   "a forward lean (negative processor z) moves +30cm along camera forward");
    CHECK_NEAR(forward.Y, 0.0, 1e-4);
    CHECK_NEAR(forward.Z, 0.0, 1e-4);

    // Sway is negated so the slide goes the same way as the turn.
    const FVector right = cb::PositionOffset(identity, 0.10f, 0.0f, 0.0f);
    CHECK_NEAR_MSG(right.Y, -10.0, 1e-4, "positive tracker x maps to -10cm on camera right");
    CHECK_NEAR(right.X, 0.0, 1e-4);
    CHECK_NEAR(right.Z, 0.0, 1e-4);

    // Heave is the one axis that is not negated.
    const FVector up = cb::PositionOffset(identity, 0.0f, 0.20f, 0.0f);
    CHECK_NEAR_MSG(up.Z, 20.0, 1e-4, "positive tracker y maps to +20cm on camera up");
    CHECK_NEAR(up.X, 0.0, 1e-4);
    CHECK_NEAR(up.Y, 0.0, 1e-4);
}

// The offset is built in the CLEAN camera frame, so head sway follows the body.
// Yawed 90 degrees, the camera's forward is world +Y.
void TestPositionOffsetIsBuiltInTheCleanCameraFrame() {
    const FQuat4d yawed = QuatFromEulerDeg(0.0, 90.0, 0.0);
    const FVector forward = cb::PositionOffset(yawed, 0.0f, 0.0f, -0.30f);
    CHECK_NEAR_MSG(forward.X, 0.0, 1e-3, "a forward lean under 90 degrees of yaw leaves world X");
    CHECK_NEAR_MSG(forward.Y, 30.0, 1e-3, "and lands on world +Y, the camera's forward");
    CHECK_NEAR(forward.Z, 0.0, 1e-3);
}

// The basis is the FULL clean rotation, pitch and roll included - not a
// horizon-locked one. So a lean follows where the camera is pointed: looking
// down a slope, leaning in also takes the eye down. That is inherited from
// still-wakes-the-deep-headtracking, whose numbers were settled in a running
// game and which this file is byte-identical to bar the namespace, so it is
// locked here rather than re-derived. Anything that changes it has to be
// re-verified in game, not argued from geometry.
void TestTheLeanFollowsAPitchedCameraRatherThanTheHorizon() {
    const FQuat4d pitchedDown = QuatFromEulerDeg(-30.0, 0.0, 0.0);
    const FVector lean = cb::PositionOffset(pitchedDown, 0.0f, 0.0f, -0.30f);
    CHECK_NEAR_MSG(lean.X, 30.0 * 0.86602540, 1e-3,
                   "a 30cm lean 30 degrees below the horizon travels 26cm forward");
    CHECK_NEAR_MSG(lean.Z, -30.0 * 0.5, 1e-3, "and 15cm down, along the pitched view");
}

// Roll is in the basis too, so a pure heave under a rolled camera is not purely
// vertical in world space.
void TestTheBasisCarriesCameraRoll() {
    const FQuat4d rolled = QuatFromEulerDeg(0.0, 0.0, 30.0);
    const FVector heave = cb::PositionOffset(rolled, 0.0f, 0.20f, 0.0f);
    CHECK_NEAR_MSG(heave.X, 0.0, 1e-3, "roll about forward leaves the forward axis alone");
    CHECK_MSG(std::fabs(heave.Y) > 5.0,
              "a rolled camera tips a pure heave sideways in world space");
    CHECK_NEAR_MSG(std::sqrt(heave.Y * heave.Y + heave.Z * heave.Z), 20.0, 1e-3,
                   "and does not change its magnitude");
}

// A centred head must not move the eye at all, in any orientation.
void TestZeroOffsetMovesNothing() {
    const FQuat4d tilted = QuatFromEulerDeg(25.0, 130.0, 12.0);
    const FVector none = cb::PositionOffset(tilted, 0.0f, 0.0f, 0.0f);
    CHECK_NEAR(none.X, 0.0, 1e-9);
    CHECK_NEAR(none.Y, 0.0, 1e-9);
    CHECK_NEAR(none.Z, 0.0, 1e-9);
}

}  // namespace

int main() {
    TestWorldSpaceYawAddsPerAxisAndNegatesRoll();
    TestWorldSpaceYawKeepsTheHorizonLevelOnAPitchedCamera();
    TestCameraLocalYawMatchesWorldSpaceOnAnUnpitchedCamera();
    TestCameraLocalYawLeansTheHorizonOnAPitchedCamera();
    TestPositionOffsetMapsAxesAndConvertsMetresToCentimetres();
    TestPositionOffsetIsBuiltInTheCleanCameraFrame();
    TestTheLeanFollowsAPitchedCameraRatherThanTheHorizon();
    TestTheBasisCarriesCameraRoll();
    TestZeroOffsetMovesNothing();
    return tow_test::Report();
}

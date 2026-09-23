// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// What the sights do to the head pose: the lean eases out, rotation is left
// alone. The fade's shape and timings are core's (cameraunlock/ads/ads_fade.h)
// and are read from there rather than restated.

#include "ads_pose.h"
#include "test_harness.h"

namespace {

using cameraunlock::ads::AdsFade;
using tow_ht::TrackingState;
using tow_ht::ads_pose::HeadPose;

HeadPose MakePose(float yaw, float pitch, float roll, float x, float y, float z) {
    HeadPose p;
    p.yaw = yaw; p.pitch = pitch; p.roll = roll;
    p.x = x; p.y = y; p.z = z;
    return p;
}

tow_ht::game_state::Verdict Gameplay() {
    tow_ht::game_state::Verdict v;
    v.InGameplay = true;
    v.GateKnown = true;
    return v;
}

// Built the way the frame walk builds it, rather than by hand.
TrackingState Aiming(bool aiming) {
    return tow_ht::DecideTracking(Gameplay(), /*trackingEnabled*/ true,
                                  /*havePose*/ true, aiming);
}

const HeadPose kHead = MakePose(20.0f, 10.0f, 4.0f, 5.0f, -3.0f, 8.0f);

void CheckRotationUntouched(const HeadPose& out) {
    CHECK_NEAR(out.yaw, kHead.yaw, 1e-6f);
    CHECK_NEAR(out.pitch, kHead.pitch, 1e-6f);
    CHECK_NEAR_MSG(out.roll, kHead.roll, 1e-6f, "roll is never faded in ADS");
}

void TestHipFirePassesThrough() {
    tow_ht::ads_pose::Reset();
    const auto out = tow_ht::ads_pose::Advance(Aiming(false), kHead, 0);
    CHECK_NEAR(out.LeanScale, 1.0f, 1e-6f);
    CheckRotationUntouched(out.Pose);
    CHECK_NEAR(out.Pose.x, kHead.x, 1e-6f);
    CHECK_NEAR(out.Pose.y, kHead.y, 1e-6f);
    CHECK_NEAR(out.Pose.z, kHead.z, 1e-6f);
}

void TestSightsUpKeepRotationAndDropTheLean() {
    tow_ht::ads_pose::Reset();
    tow_ht::ads_pose::Advance(Aiming(true), kHead, 0);
    const auto out = tow_ht::ads_pose::Advance(Aiming(true), kHead, AdsFade::kLowerMs * 2);
    CHECK_NEAR(out.LeanScale, 0.0f, 1e-6f);
    CheckRotationUntouched(out.Pose);
    CHECK_NEAR(out.Pose.x, 0.0f, 1e-6f);
    CHECK_NEAR(out.Pose.y, 0.0f, 1e-6f);
    CHECK_NEAR(out.Pose.z, 0.0f, 1e-6f);
}

void TestMidTransitionScalesOnlyTheLean() {
    tow_ht::ads_pose::Reset();
    tow_ht::ads_pose::Advance(Aiming(true), kHead, 0);
    const auto out = tow_ht::ads_pose::Advance(Aiming(true), kHead, AdsFade::kLowerMs / 2);
    CHECK(out.LeanScale > 0.0f && out.LeanScale < 1.0f);
    CheckRotationUntouched(out.Pose);
    CHECK_NEAR(out.Pose.x, kHead.x * out.LeanScale, 1e-5f);
    CHECK_NEAR(out.Pose.y, kHead.y * out.LeanScale, 1e-5f);
    CHECK_NEAR(out.Pose.z, kHead.z * out.LeanScale, 1e-5f);
}

// A tap of the aim button: the reversal continues from where the transition
// is, so the lean does not step.
void TestReversalContinuesFromWhereItIs() {
    tow_ht::ads_pose::Reset();
    tow_ht::ads_pose::Advance(Aiming(true), kHead, 0);
    const auto down = tow_ht::ads_pose::Advance(Aiming(true), kHead, AdsFade::kLowerMs / 2);
    const auto back = tow_ht::ads_pose::Advance(Aiming(false), kHead, AdsFade::kLowerMs / 2);
    CHECK_NEAR(back.LeanScale, down.LeanScale, 1e-3f);
    CHECK_NEAR(back.Pose.x, down.Pose.x, 1e-2f);
    const auto hip = tow_ht::ads_pose::Advance(
        Aiming(false), kHead, AdsFade::kLowerMs / 2 + AdsFade::kRaiseMs);
    CHECK_NEAR(hip.LeanScale, 1.0f, 1e-6f);
    CHECK_NEAR(hip.Pose.x, kHead.x, 1e-5f);
}

// Suppression drops the transition, so the lean is back at the hip on the next
// frame rather than still eased out.
void TestResetReturnsToTheHip() {
    tow_ht::ads_pose::Reset();
    tow_ht::ads_pose::Advance(Aiming(true), kHead, 0);
    tow_ht::ads_pose::Advance(Aiming(true), kHead, AdsFade::kLowerMs * 2);
    tow_ht::ads_pose::Reset();
    const auto out = tow_ht::ads_pose::Advance(Aiming(false), kHead, AdsFade::kLowerMs * 2);
    CHECK_NEAR(out.LeanScale, 1.0f, 1e-6f);
}

}  // namespace

int main() {
    TestHipFirePassesThrough();
    TestSightsUpKeepRotationAndDropTheLean();
    TestMidTransitionScalesOnlyTheLean();
    TestReversalContinuesFromWhereItIs();
    TestResetReturnsToTheHip();

    return tow_test::Report();
}

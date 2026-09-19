// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// What the sights do to the head pose.
//
// The entry pose is what makes the tracked ADS modes swing onto the aim and then
// keep tracking from there, and it is the piece of the shared ADS module this
// mod feeds directly out of ads_pose.cpp - engine degrees and engine position
// units, straight from the camera boundary and before the zoom compensation. Its
// rules are pinned at the source too (cameraunlock-core); these are the cases
// this mod would break on, and they cost nothing to run.
//
// The cycle strings, the fade shape and the transition timings ARE exercised
// here, but only through the values this mod ships and only to pin what it
// depends on - that all three of its cycle slots parse, and that the fade eases
// rather than cutting. Their definitions stay in the core; nothing here restates
// a timing constant.

#include "ads.h"
#include "ads_pose.h"
#include "test_harness.h"

namespace {

using tow_ht::AdsEntryPose;
using tow_ht::AdsFade;
using tow_ht::AdsMode;
using tow_ht::BlendAdsPose;
using tow_ht::TrackingState;
using tow_ht::TrackingVerdict;

AdsEntryPose::Pose MakePose(float pitch, float yaw, float roll,
                            float x = 0.0f, float y = 0.0f, float z = 0.0f) {
    AdsEntryPose::Pose p;
    p.pitch = pitch; p.yaw = yaw; p.roll = roll;
    p.x = x; p.y = y; p.z = z;
    return p;
}

tow_ht::game_state::Verdict Gameplay() {
    tow_ht::game_state::Verdict v;
    v.InGameplay = true;
    v.GateKnown = true;
    return v;
}

// Built the way the frame walk builds it, rather than by hand. A hand-made state
// that always pairs `aiming` with AdsSuspended makes the two interchangeable to
// every test in this file, and the fade is then free to read the VERDICT instead
// of the sights without anything going red - which would break `tracked`
// in game, since aiming yields Active and the fade
// would never leave the hip.
TrackingState Aiming(bool aiming) {
    return tow_ht::DecideTracking(Gameplay(), /*trackingEnabled*/ true,
                                  /*havePose*/ true, aiming, tow_ht::GetAdsMode());
}

// Hip fire is untouched: whatever the tracker says reaches the camera.
void TestHipFirePassesThrough() {
    AdsEntryPose entry;
    const auto out = entry.Relative(false, true, MakePose(5.0f, -12.0f, 3.0f, 1.0f, 2.0f, 3.0f));
    CHECK_NEAR(out.pitch, 5.0f, 1e-6f);
    CHECK_NEAR(out.yaw, -12.0f, 1e-6f);
    CHECK_NEAR(out.roll, 3.0f, 1e-6f);
    CHECK_NEAR(out.x, 1.0f, 1e-6f);
    CHECK(!entry.HasEntry());
}

// The entry frame is identity, which is what puts the view on the point the shot
// was already going to rather than wherever the head happens to be.
void TestEntryFrameIsIdentity() {
    AdsEntryPose entry;
    const auto out = entry.Relative(true, true, MakePose(20.0f, -35.0f, 0.0f, 4.0f, 5.0f, 6.0f));
    CHECK(entry.HasEntry());
    CHECK_NEAR(out.pitch, 0.0f, 1e-6f);
    CHECK_NEAR(out.yaw, 0.0f, 1e-6f);
    CHECK_NEAR(out.x, 0.0f, 1e-6f);
    CHECK_NEAR(out.y, 0.0f, 1e-6f);
    CHECK_NEAR(out.z, 0.0f, 1e-6f);
}

// Roll moves no aim point, so zeroing it would yank a head tilt the player is
// actively holding back to level and lean it in again as they move: two horizon
// jolts per aim, buying nothing.
void TestRollStaysAbsolute() {
    AdsEntryPose entry;
    CHECK_NEAR(entry.Relative(true, true, MakePose(0.0f, 0.0f, 14.0f)).roll, 14.0f, 1e-6f);
    CHECK_NEAR(entry.Relative(true, true, MakePose(0.0f, 0.0f, -6.0f)).roll, -6.0f, 1e-6f);
}

// Yaw arrives wrapped into -180..180, so a plain subtraction reads a 10 degree
// move across the seam as -350 and whips the view a full turn the wrong way.
void TestYawCrossesTheSeamTheShortWay() {
    AdsEntryPose entry;
    entry.Relative(true, true, MakePose(0.0f, 175.0f, 0.0f));
    CHECK_NEAR(entry.Relative(true, true, MakePose(0.0f, -175.0f, 0.0f)).yaw, 10.0f, 1e-4f);

    AdsEntryPose back;
    back.Relative(true, true, MakePose(0.0f, -175.0f, 0.0f));
    CHECK_NEAR(back.Relative(true, true, MakePose(0.0f, 175.0f, 0.0f)).yaw, -10.0f, 1e-4f);
}

// Pitch is bounded by the tracker's own asin and cannot wrap, so it stays a
// plain difference - and the lean goes relative with it.
void TestPitchAndPositionAreRelative() {
    AdsEntryPose entry;
    entry.Relative(true, true, MakePose(10.0f, 0.0f, 0.0f, 3.0f, -1.0f, 2.0f));
    const auto out = entry.Relative(true, true, MakePose(-5.0f, 0.0f, 0.0f, 4.0f, -3.0f, 2.5f));
    CHECK_NEAR(out.pitch, -15.0f, 1e-6f);
    CHECK_NEAR(out.x, 1.0f, 1e-6f);
    CHECK_NEAR(out.y, -2.0f, 1e-6f);
    CHECK_NEAR(out.z, 0.5f, 1e-6f);
}

// Lowering the weapon drops it, so the next aim re-enters from wherever the head
// is then rather than from a pose two firefights old.
void TestLoweringTheWeaponDropsTheEntry() {
    AdsEntryPose entry;
    entry.Relative(true, true, MakePose(10.0f, 40.0f, 0.0f));
    const auto down = entry.Relative(false, true, MakePose(12.0f, 45.0f, 0.0f));
    CHECK(!entry.HasEntry());
    CHECK_NEAR(down.yaw, 45.0f, 1e-6f);
    CHECK_NEAR(entry.Relative(true, true, MakePose(12.0f, 45.0f, 0.0f)).yaw, 0.0f, 1e-6f);
}

// At the hip every mode hands the camera the head pose whole.
void TestBlendHipIsTheHeadPose() {
    const auto absolute = MakePose(5.0f, -12.0f, 3.0f, 1.0f, 2.0f, 3.0f);
    const auto relative = MakePose(0.0f, 0.0f, 3.0f);
    for (const AdsMode mode : { AdsMode::Paused, AdsMode::Tracked }) {
        const auto out = BlendAdsPose(mode, 1.0f, absolute, relative);
        CHECK_NEAR(out.pitch, 5.0f, 1e-6f);
        CHECK_NEAR(out.yaw, -12.0f, 1e-6f);
        CHECK_NEAR(out.roll, 3.0f, 1e-6f);
        CHECK_NEAR(out.z, 3.0f, 1e-6f);
    }
}

// Sights fully up in `paused`: the view is the game's again, apart from the tilt
// the player is holding.
void TestBlendPausedKeepsRollAndDropsTheRest() {
    const auto absolute = MakePose(5.0f, -12.0f, 3.0f, 1.0f, 2.0f, 3.0f);
    const auto out = BlendAdsPose(AdsMode::Paused, 0.0f, absolute, MakePose(0.0f, 0.0f, 3.0f));
    CHECK_NEAR(out.pitch, 0.0f, 1e-6f);
    CHECK_NEAR(out.yaw, 0.0f, 1e-6f);
    CHECK_NEAR(out.x, 0.0f, 1e-6f);
    CHECK_NEAR(out.y, 0.0f, 1e-6f);
    CHECK_NEAR(out.z, 0.0f, 1e-6f);
    CHECK_NEAR(out.roll, 3.0f, 1e-6f);
}

// Tracked mode lands on the entry-relative pose, so head tracking carries
// on from the aim rather than from centre.
void TestBlendTrackedLandsOnTheEntryRelativePose() {
    const auto absolute = MakePose(5.0f, -12.0f, 3.0f, 1.0f, 2.0f, 3.0f);
    const auto relative = MakePose(2.0f, -4.0f, 3.0f, 0.5f, 0.5f, 1.0f);
    const auto out = BlendAdsPose(AdsMode::Tracked, 0.0f, absolute, relative);
    CHECK_NEAR(out.pitch, 2.0f, 1e-6f);
    CHECK_NEAR(out.yaw, -4.0f, 1e-6f);
    CHECK_NEAR(out.x, 0.5f, 1e-6f);
    CHECK_NEAR(out.z, 1.0f, 1e-6f);
    CHECK_NEAR(out.roll, 3.0f, 1e-6f);
}

// The entry is a fade, not a switch: raising the sights eases the pose off over
// AdsFade::kLowerMs rather than cutting it in one frame.
void TestFadeEasesRatherThanCutting() {
    AdsFade fade;
    CHECK_NEAR(fade.Update(false, 1000), 1.0f, 1e-6f);
    CHECK_NEAR(fade.Update(true, 1000), 1.0f, 1e-6f);
    const float part = fade.Update(true, 1000 + AdsFade::kLowerMs / 2);
    CHECK(part > 0.0f && part < 1.0f);
    CHECK_NEAR(fade.Update(true, 1000 + AdsFade::kLowerMs * 2), 0.0f, 1e-6f);
}

// A tap of the aim button releases while the pose is part way off. The reversal
// has to start from where the transition is, or the view steps by however far
// the interrupted leg had travelled.
void TestFadeReversalStartsWhereItIs() {
    AdsFade fade;
    fade.Update(false, 0);
    fade.Update(true, 0);
    const float part = fade.Update(true, AdsFade::kLowerMs / 2);
    CHECK(part > 0.0f && part < 1.0f);
    const float back = fade.Update(false, AdsFade::kLowerMs / 2);
    CHECK_NEAR(back, part, 1e-3f);
    CHECK_NEAR(fade.Update(false, AdsFade::kLowerMs / 2 + AdsFade::kRaiseMs), 1.0f, 1e-6f);
}

// The frame walk reads the mode live rather than caching it, so a cycle mid-aim
// lands on the aim already in progress. Paused runs the pose down to nothing.
void TestAdvancePausedRunsThePoseDown() {
    tow_ht::ads_pose::Reset();
    tow_ht::SetAdsMode(AdsMode::Paused);

    tow_ht::ads_pose::Advance(Aiming(false), MakePose(0.0f, 0.0f, 0.0f), 0);
    const auto entered = tow_ht::ads_pose::Advance(
        Aiming(true), MakePose(10.0f, 20.0f, 4.0f), 0);
    CHECK_NEAR(entered.Scale, 1.0f, 1e-6f);
    CHECK_NEAR(entered.Pose.pitch, 10.0f, 1e-6f);

    const unsigned long long settled = AdsFade::kLowerMs * 2;
    const auto paused = tow_ht::ads_pose::Advance(
        Aiming(true), MakePose(15.0f, 25.0f, 4.0f), settled);
    CHECK_NEAR(paused.Scale, 0.0f, 1e-6f);
    CHECK_NEAR(paused.Pose.pitch, 0.0f, 1e-6f);
    CHECK_NEAR(paused.Pose.yaw, 0.0f, 1e-6f);
    CHECK_NEAR_MSG(paused.Pose.roll, 4.0f, 1e-6f,
                   "roll is in neither fade, so a held tilt survives the aim");
}

// Cycled to a tracked mode on the very next frame, the same aim settles onto the
// entry-relative pose instead.
void TestAdvanceTrackedLandsOnTheEntryRelativePose() {
    tow_ht::ads_pose::Reset();
    tow_ht::SetAdsMode(AdsMode::Tracked);

    tow_ht::ads_pose::Advance(Aiming(false), MakePose(0.0f, 0.0f, 0.0f), 0);
    tow_ht::ads_pose::Advance(Aiming(true), MakePose(10.0f, 20.0f, 0.0f), 0);

    const unsigned long long settled = AdsFade::kLowerMs * 2;
    const auto out = tow_ht::ads_pose::Advance(
        Aiming(true), MakePose(14.0f, 26.0f, 0.0f), settled);
    CHECK_NEAR(out.Pose.pitch, 4.0f, 1e-6f);
    CHECK_NEAR(out.Pose.yaw, 6.0f, 1e-6f);
}

// Aiming in a tracked mode leaves the gate OPEN - the verdict is Active, not
// AdsSuspended - and the pose still has to swing onto the aim. A fade that read
// the verdict rather than the sights would sit at the hip through this.
void TestATrackedAimIsActiveAndStillSwingsOntoTheAim() {
    tow_ht::ads_pose::Reset();
    tow_ht::SetAdsMode(AdsMode::Tracked);

    const TrackingState up = Aiming(true);
    CHECK_MSG(up.verdict == TrackingVerdict::Active,
              "aiming in tracked mode keeps tracking active");
    CHECK(up.aiming);

    tow_ht::ads_pose::Advance(Aiming(false), MakePose(0.0f, 0.0f, 0.0f), 0);
    tow_ht::ads_pose::Advance(up, MakePose(10.0f, 20.0f, 0.0f), 0);
    const auto out = tow_ht::ads_pose::Advance(
        up, MakePose(14.0f, 26.0f, 0.0f), AdsFade::kLowerMs * 2);
    CHECK_NEAR_MSG(out.Pose.pitch, 4.0f, 1e-6f,
                   "the aim settles onto the entry-relative pose, not the absolute one");
    CHECK_NEAR(out.Pose.yaw, 6.0f, 1e-6f);
}

// Everything the mod suspends for - a menu, a conversation, a loading screen,
// the master toggle, a dead tracker - drops the entry pose and the transition,
// so the next aim re-enters cleanly instead of resuming against a pose from
// before the suppression.
void TestResetDropsTheEntryPose() {
    tow_ht::ads_pose::Reset();
    tow_ht::SetAdsMode(AdsMode::Tracked);

    tow_ht::ads_pose::Advance(Aiming(true), MakePose(10.0f, 40.0f, 0.0f), 0);
    tow_ht::ads_pose::Reset();

    const unsigned long long settled = AdsFade::kLowerMs * 2;
    tow_ht::ads_pose::Advance(Aiming(true), MakePose(30.0f, 80.0f, 0.0f), 0);
    const auto out = tow_ht::ads_pose::Advance(
        Aiming(true), MakePose(30.0f, 80.0f, 0.0f), settled);
    CHECK_NEAR(out.Pose.pitch, 0.0f, 1e-6f);
    CHECK_NEAR(out.Pose.yaw, 0.0f, 1e-6f);

    // The transition too, not just the pose. Reset mid-aim then lower the
    // weapon: the fade has to be back at the hip, which it is not if Reset only
    // dropped the entry.
    tow_ht::ads_pose::Reset();
    tow_ht::ads_pose::Advance(Aiming(true), MakePose(30.0f, 80.0f, 0.0f), 0);
    tow_ht::ads_pose::Advance(Aiming(true), MakePose(30.0f, 80.0f, 0.0f), settled);
    tow_ht::ads_pose::Reset();
    const auto hip = tow_ht::ads_pose::Advance(
        Aiming(false), MakePose(30.0f, 80.0f, 0.0f), settled);
    CHECK_NEAR_MSG(hip.Scale, 1.0f, 1e-6f,
                   "Reset drops the transition, so the next frame is back at the hip");
}

// An unrecognised string is the default rather than whichever branch is last -
// a typo in a hand-edited file, and the migration path for a mode renamed since
// an older release wrote the setting.
void TestUnknownModeStringFallsBackToPaused() {
    CHECK(tow_ht::NextAdsModeTwoSlot(AdsMode::Paused) == AdsMode::Tracked);
    CHECK(tow_ht::NextAdsModeTwoSlot(AdsMode::Tracked) == AdsMode::Paused);
    CHECK(tow_ht::ParseAdsMode("paused") == AdsMode::Paused);
    CHECK(tow_ht::ParseAdsMode("marker") == AdsMode::Paused);
    CHECK(tow_ht::ParseAdsMode("tracked") == AdsMode::Tracked);
    CHECK(tow_ht::ParseAdsMode("  TRACKED\r\n") == AdsMode::Tracked);
    CHECK(tow_ht::ParseAdsMode("") == AdsMode::Paused);
    CHECK(tow_ht::ParseAdsMode("finealim") == AdsMode::Paused);
    CHECK(tow_ht::ParseAdsMode(nullptr) == AdsMode::Paused);
}

}  // namespace

int main() {
    TestHipFirePassesThrough();
    TestEntryFrameIsIdentity();
    TestRollStaysAbsolute();
    TestYawCrossesTheSeamTheShortWay();
    TestPitchAndPositionAreRelative();
    TestLoweringTheWeaponDropsTheEntry();
    TestBlendHipIsTheHeadPose();
    TestBlendPausedKeepsRollAndDropsTheRest();
    TestBlendTrackedLandsOnTheEntryRelativePose();
    TestFadeEasesRatherThanCutting();
    TestFadeReversalStartsWhereItIs();
    TestAdvancePausedRunsThePoseDown();
    TestAdvanceTrackedLandsOnTheEntryRelativePose();
    TestATrackedAimIsActiveAndStillSwingsOntoTheAim();
    TestResetDropsTheEntryPose();
    TestUnknownModeStringFallsBackToPaused();

    return tow_test::Report();
}

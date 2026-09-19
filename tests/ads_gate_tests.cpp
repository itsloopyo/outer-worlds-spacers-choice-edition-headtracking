// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The verdict walk: whether the head pose reaches the view, and what the frame
// reports about the sights while it decides.
//
// ADS is tested LAST in that walk, so a menu, dialogue or a dead tracker still
// names its own reason when both are true at once, and no early return may leave
// the sights flag set - a stale flag through a conversation would hold the pose
// to the aim it was blended into against a weapon that is not raised.

#include "ads_gate.h"
#include "test_harness.h"

namespace {

using tow_ht::AdsMode;
using tow_ht::DecideTracking;
using tow_ht::PoseApplies;
using tow_ht::Reason;
using tow_ht::TrackingVerdict;
using tow_ht::game_state::Verdict;

// The gate as it reads with the player in first-person control: the cursor flag
// is down and it was readable.
Verdict Gameplay() {
    Verdict v;
    v.InGameplay = true;
    v.GateKnown = true;
    return v;
}

// `paused` closes the gate on the aim and still reports the sights: the gate
// says whether tracking applies, the flag says what the weapon is doing.
void TestPausedClosesTheGateAndStillReportsTheSights() {
    const auto s = DecideTracking(Gameplay(), true, true, true, AdsMode::Paused);
    CHECK(s.verdict == TrackingVerdict::AdsSuspended);
    CHECK(s.aiming);
    // A pose still reaches the camera, because suspending is an ease-out rather
    // than a switch. Dropping it on the falling edge would throw the smoothing
    // state away and swing the view back through the head angle on the way out.
    CHECK(PoseApplies(s.verdict));
}

void TestTrackedModesStayOpenThroughAnAim() {
    const auto s = DecideTracking(Gameplay(), true, true, true, AdsMode::Tracked);
    CHECK(s.verdict == TrackingVerdict::Active);
    CHECK(s.aiming);
    CHECK(PoseApplies(s.verdict));
}

void TestHipFireIsActiveInEveryMode() {
    for (const AdsMode mode : { AdsMode::Paused, AdsMode::Tracked }) {
        const auto s = DecideTracking(Gameplay(), true, true, false, mode);
        CHECK(s.verdict == TrackingVerdict::Active);
        CHECK(!s.aiming);
    }
}

// A menu, the inventory or a loading screen outranks ADS in the reported
// reason, and clears the sights flag with it.
void TestMenuOutranksAdsAndClearsTheFlag() {
    Verdict gate = Gameplay();
    gate.InGameplay = false;
    for (const AdsMode mode : { AdsMode::Paused, AdsMode::Tracked }) {
        const auto s = DecideTracking(gate, true, true, true, mode);
        CHECK(s.verdict == TrackingVerdict::NotGameplay);
        CHECK(!s.aiming);
        CHECK(!PoseApplies(s.verdict));
    }
}

// A conversation raises the cursor like a menu does, but the head pose still
// reaches the view. It never reports the sights: the pose goes on unshaped, and
// a stale flag would hold it to an aim against a weapon that is not raised.
void TestConversationAppliesThePoseAndClearsTheSights() {
    Verdict gate = Gameplay();
    gate.InGameplay = false;
    gate.InConversation = true;
    for (const AdsMode mode : { AdsMode::Paused, AdsMode::Tracked }) {
        const auto s = DecideTracking(gate, true, true, true, mode);
        CHECK(s.verdict == TrackingVerdict::Conversation);
        CHECK(!s.aiming);
    }
}

// A conversation with the tracker silent is the tracker's fault, and says so.
void TestConversationWithoutATrackerReportsNoTracker() {
    Verdict gate = Gameplay();
    gate.InGameplay = false;
    gate.InConversation = true;
    const auto s = DecideTracking(gate, true, false, false, AdsMode::Paused);
    CHECK(s.verdict == TrackingVerdict::NoTracker);
}

// The master toggle switches tracking off in a conversation as well.
void TestMasterToggleOutranksAConversation() {
    Verdict gate = Gameplay();
    gate.InGameplay = false;
    gate.InConversation = true;
    const auto s = DecideTracking(gate, false, true, false, AdsMode::Paused);
    CHECK(s.verdict == TrackingVerdict::Disabled);
}

// An unreadable cursor flag arrives here as InGameplay false - game_state fails
// closed - so it lands on the same branch and must not report the sights either.
void TestUnreadableGateFailsClosed() {
    Verdict gate;
    gate.InGameplay = false;
    gate.GateKnown = false;
    const auto s = DecideTracking(gate, true, true, true, AdsMode::Tracked);
    CHECK(s.verdict == TrackingVerdict::NotGameplay);
    CHECK(!s.aiming);
    CHECK(!PoseApplies(s.verdict));
}

// The master toggle outranks everything, and reports its own reason.
void TestMasterToggleOutranksAds() {
    const auto s = DecideTracking(Gameplay(), false, true, true, AdsMode::Tracked);
    CHECK(s.verdict == TrackingVerdict::Disabled);
    CHECK(!s.aiming);
    CHECK(!PoseApplies(s.verdict));
}

// No tracker is not an ADS verdict either, and it must not report the sights.
void TestNoTrackerReportsItsOwnReason() {
    const auto s = DecideTracking(Gameplay(), true, false, true, AdsMode::Tracked);
    CHECK(s.verdict == TrackingVerdict::NoTracker);
    CHECK(!s.aiming);
    CHECK(!PoseApplies(s.verdict));
}

// The state is recomputed from the game every frame rather than latched on an
// edge, so an exit event that never arrives - an aim released while firing, a
// state machine that transitions without one - heals on the next frame instead
// of stranding the player in ADS behaviour.
void TestAdsHealsWithoutAnExitEdge() {
    const auto aimed = DecideTracking(Gameplay(), true, true, true, AdsMode::Paused);
    CHECK(aimed.verdict == TrackingVerdict::AdsSuspended);
    const auto healed = DecideTracking(Gameplay(), true, true, false, AdsMode::Paused);
    CHECK(healed.verdict == TrackingVerdict::Active);
    CHECK(!healed.aiming);
}

// A mode cycled mid-aim is read on the next frame's walk, so it lands on the aim
// that is already in progress rather than on the next one.
void TestCyclingMidAimChangesTheVerdict() {
    CHECK(DecideTracking(Gameplay(), true, true, true, AdsMode::Paused).verdict
          == TrackingVerdict::AdsSuspended);
    CHECK(DecideTracking(Gameplay(), true, true, true, AdsMode::Tracked).verdict
          == TrackingVerdict::Active);
}

// Every verdict names itself, because the heartbeat line is what a player is
// asked to send when tracking "just stops".
void TestEveryVerdictHasAReason() {
    for (const TrackingVerdict v : { TrackingVerdict::Active,
                                     TrackingVerdict::AdsSuspended,
                                     TrackingVerdict::Disabled,
                                     TrackingVerdict::Conversation,
                                     TrackingVerdict::NotGameplay,
                                     TrackingVerdict::NoTracker }) {
        const char* reason = Reason(v);
        CHECK(reason != nullptr && reason[0] != '\0');
    }
}

}  // namespace

int main() {
    TestPausedClosesTheGateAndStillReportsTheSights();
    TestTrackedModesStayOpenThroughAnAim();
    TestHipFireIsActiveInEveryMode();
    TestMenuOutranksAdsAndClearsTheFlag();
    TestConversationAppliesThePoseAndClearsTheSights();
    TestConversationWithoutATrackerReportsNoTracker();
    TestMasterToggleOutranksAConversation();
    TestUnreadableGateFailsClosed();
    TestMasterToggleOutranksAds();
    TestNoTrackerReportsItsOwnReason();
    TestAdsHealsWithoutAnExitEdge();
    TestCyclingMidAimChangesTheVerdict();
    TestEveryVerdictHasAReason();

    return tow_test::Report();
}

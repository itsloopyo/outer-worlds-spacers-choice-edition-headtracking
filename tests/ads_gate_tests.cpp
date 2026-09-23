// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The verdict walk: whether the head pose reaches the view, and what the frame
// reports about the sights while it decides.
//
// The sights never close the gate. ADS is tested LAST in the walk, so a menu,
// dialogue or a dead tracker still names its own reason when both are true at
// once, and no early return may leave the sights flag set - a stale flag would
// hold the lean eased out against a weapon that is not raised.

#include <initializer_list>

#include "ads_gate.h"
#include "test_harness.h"

namespace {

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

void TestAimingKeepsTrackingActive() {
    const auto s = DecideTracking(Gameplay(), true, true, true);
    CHECK_MSG(s.verdict == TrackingVerdict::Active,
              "raising the sights does not close the gate");
    CHECK(s.aiming);
    CHECK(PoseApplies(s.verdict));
}

void TestHipFireIsActive() {
    const auto s = DecideTracking(Gameplay(), true, true, false);
    CHECK(s.verdict == TrackingVerdict::Active);
    CHECK(!s.aiming);
}

// A menu, the inventory or a loading screen outranks ADS in the reported
// reason, and clears the sights flag with it.
void TestMenuOutranksAdsAndClearsTheFlag() {
    Verdict gate = Gameplay();
    gate.InGameplay = false;
    const auto s = DecideTracking(gate, true, true, true);
    CHECK(s.verdict == TrackingVerdict::NotGameplay);
    CHECK(!s.aiming);
    CHECK(!PoseApplies(s.verdict));
}

// A conversation raises the cursor like a menu does, but the head pose still
// reaches the view. It never reports the sights.
void TestConversationAppliesThePoseAndClearsTheSights() {
    Verdict gate = Gameplay();
    gate.InGameplay = false;
    gate.InConversation = true;
    const auto s = DecideTracking(gate, true, true, true);
    CHECK(s.verdict == TrackingVerdict::Conversation);
    CHECK(!s.aiming);
}

// A conversation with the tracker silent is the tracker's fault, and says so.
void TestConversationWithoutATrackerReportsNoTracker() {
    Verdict gate = Gameplay();
    gate.InGameplay = false;
    gate.InConversation = true;
    const auto s = DecideTracking(gate, true, false, false);
    CHECK(s.verdict == TrackingVerdict::NoTracker);
}

// The master toggle switches tracking off in a conversation as well.
void TestMasterToggleOutranksAConversation() {
    Verdict gate = Gameplay();
    gate.InGameplay = false;
    gate.InConversation = true;
    const auto s = DecideTracking(gate, false, true, false);
    CHECK(s.verdict == TrackingVerdict::Disabled);
}

// An unreadable cursor flag arrives here as InGameplay false - game_state fails
// closed - so it lands on the same branch and must not report the sights either.
void TestUnreadableGateFailsClosed() {
    Verdict gate;
    gate.InGameplay = false;
    gate.GateKnown = false;
    const auto s = DecideTracking(gate, true, true, true);
    CHECK(s.verdict == TrackingVerdict::NotGameplay);
    CHECK(!s.aiming);
    CHECK(!PoseApplies(s.verdict));
}

// The master toggle outranks everything, and reports its own reason.
void TestMasterToggleOutranksAds() {
    const auto s = DecideTracking(Gameplay(), false, true, true);
    CHECK(s.verdict == TrackingVerdict::Disabled);
    CHECK(!s.aiming);
    CHECK(!PoseApplies(s.verdict));
}

// No tracker is not an ADS verdict either, and it must not report the sights.
void TestNoTrackerReportsItsOwnReason() {
    const auto s = DecideTracking(Gameplay(), true, false, true);
    CHECK(s.verdict == TrackingVerdict::NoTracker);
    CHECK(!s.aiming);
    CHECK(!PoseApplies(s.verdict));
}

// The state is recomputed from the game every frame rather than latched on an
// edge, so an exit event that never arrives heals on the next frame.
void TestAdsHealsWithoutAnExitEdge() {
    CHECK(DecideTracking(Gameplay(), true, true, true).aiming);
    const auto healed = DecideTracking(Gameplay(), true, true, false);
    CHECK(healed.verdict == TrackingVerdict::Active);
    CHECK(!healed.aiming);
}

// Every verdict names itself, because the heartbeat line is what a player is
// asked to send when tracking "just stops".
void TestEveryVerdictHasAReason() {
    for (const TrackingVerdict v : { TrackingVerdict::Active,
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
    TestAimingKeepsTrackingActive();
    TestHipFireIsActive();
    TestMenuOutranksAdsAndClearsTheFlag();
    TestConversationAppliesThePoseAndClearsTheSights();
    TestConversationWithoutATrackerReportsNoTracker();
    TestMasterToggleOutranksAConversation();
    TestUnreadableGateFailsClosed();
    TestMasterToggleOutranksAds();
    TestNoTrackerReportsItsOwnReason();
    TestAdsHealsWithoutAnExitEdge();
    TestEveryVerdictHasAReason();

    return tow_test::Report();
}

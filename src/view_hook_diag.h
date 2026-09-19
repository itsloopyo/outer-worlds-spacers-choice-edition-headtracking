// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include "ads_gate.h"
#include "aim_trace.h"
#include "game_state.h"
#include "session.h"

#include "cameraunlock/unreal/ue_math.h"

// What the GetPlayerViewPoint hook writes to the log.
//
// Separated from the frame walk because it is a different job with a different
// lifetime: the walk decides what the player sees this frame, these decide what
// a triage session can read off HeadTracking.log afterwards, and each carries
// its own cadence state that the walk has no business in. Every one of them is
// rate limited, because the caller runs at the player's frame rate and each
// line is an unbuffered locked write on the game thread.
//
// Every input arrives as an argument. Nothing here reaches back into the hook's
// own state, so the hook's toggles stay private to it and a line's contents can
// be read off its call site.
namespace tow_ht::view_hook::diag {

// Count one GetPlayerViewPoint return address and, on an interval, print the
// table of every distinct one seen. This is how the render-path caller is
// (re-)identified after a patch, so it is only driven in the all-callers
// diagnostic mode.
void CountCaller(std::uintptr_t retRva, std::uint64_t call);

// Say so, once, if the hook is ever entered from a second thread.
//
// The whole frame walk is written as though it is not: the frame clock, the lean
// clamp's allowance, the pawn-offset latch and the shared Kismet parameter frames
// are all plain read-modify-write with no synchronisation, and adding a lock to
// the render path is not an option. Nothing in this repo records that assumption
// having been measured, so it is measured here rather than asserted in a comment
// - two relaxed atomics on the entry path, and a line in the log if it is ever
// wrong. If this line appears, the walk needs a thread gate, not per-variable
// atomics: the state it shares is engine-dispatch buffers, not scalars.
void NoteHookThread();

// Everything the heartbeat names, gathered by the caller for one render frame.
//
// The session is passed rather than the frame's own pose: the heartbeat asks it
// for a rotation whether or not the frame's Update() reported one, and the two
// answers are not the same claim.
struct HeartbeatInputs {
    std::uint64_t  Call = 0;
    std::uintptr_t RetRva = 0;
    game_state::Verdict Gate;
    TrackingState State;
    Session* Tracking = nullptr;
    bool TrackingEnabled = false;
    bool WorldSpaceYaw = false;
    int  InjectMode = 0;
    bool CollisionEnabled = false;
    bool LeanInContact = false;
};

// One line every kHeartbeatMs, from the render caller only, so it describes the
// frame the player is looking at rather than whichever caller the interval
// happened to land on. The first goes out on the first render frame rather than
// at hook call #1, which is some other caller.
void Heartbeat(const HeartbeatInputs& in);

// What a caller the gate REJECTED walked away with.
//
// This is the decoupling claim written down where it can be checked rather than
// asserted: the weapon trace, the interaction ray and the audio listener all ask
// through one of these, and every one of them has to leave with the clean
// mouse/pad rotation while the drawn frame carries the head pose. A line whose
// rotation matches the render caller's `clean=` and not its `result=` is what
// says the shot still goes where the mouse pointed.
void LogDecouplingSample(bool poseLog, bool trackingEnabled, std::uint64_t call,
                         std::uintptr_t retRva,
                         const cameraunlock::unreal::FRotator& out);

// One rendered frame's pose, from the raw tracker sample to what reached the
// camera, plus where the shot lands.
struct PoseSample {
    std::uint64_t Call;
    std::uintptr_t RetRva;
    cameraunlock::unreal::FRotator Clean;
    cameraunlock::unreal::FRotator Result;
    double Yaw, Pitch, Roll;
    float OffsetX, OffsetY, OffsetZ;
    cameraunlock::unreal::FVector Applied;
    float Zoom;
    aim_trace::Result Aim;
};

// Time-gated rather than call-gated because this fires on the render caller, so
// a call-count interval would log at the player's frame rate. Capped at
// kPoseDetailLines unless `uncapped` ([Dev] PoseLog) lifts it for a measuring
// session.
void LogPoseDetail(bool uncapped, const PoseSample& s);

// Lean-clamp contact and query-failure transitions, plus a periodic sample:
// transitions alone cannot tell "the sweep runs and the room is open" from "the
// sweep is not running", and those need different fixes.
void LogLeanClampState(bool inContact, bool queryFailed);

// Collision is on in the ini and the sweep cannot run, because the profile's
// reflection layout has not been proved. Said once, then sampled, for the same
// reason LogLeanClampState samples: one line at startup cannot be told apart
// from a line that never came.
void LogClampUnavailable();

// The tracker arriving and going away, written the moment it happens. The
// heartbeat samples the same flag, but at 30s it cannot place a dropout inside
// the minute a player reports it in.
void LogTrackerConnection(bool havePose);

// The widget probe, on a timer. Repeated rather than fired once because the HUD
// is not built on the first frames, and a single early pass would list nothing.
//
// Gated on gameplay, and that is the point of it: the crosshair does not exist
// as a live object in the main menu, so a probe that spent its passes there
// would list a few hundred packages and never the widget it was run for.
void RunWidgetProbe(bool inGameplay, const char* outerContains);

}  // namespace tow_ht::view_hook::diag

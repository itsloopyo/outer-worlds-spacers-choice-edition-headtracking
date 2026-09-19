// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "game_state.h"

#include <cstdint>

#include <windows.h>

#include "builds/build_registry.h"
#include "conversation_state.h"
#include "logging.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace tow_ht::game_state {

namespace {

namespace ue = ::cameraunlock::unreal;

bool g_haveLast = false;
bool g_lastInGameplay = false;
bool g_lastKnown = false;
bool g_lastConversation = false;
std::uint64_t g_lastLineMs = 0;

// The gate flips both ways while a controller is torn down and rebuilt across a
// map transition, and every line is a locked, unbuffered write on the render
// caller. Hold the verdict, defer the line - the state it settles on still gets
// written, the same way the tracker-connection line is paced.
constexpr std::uint64_t kGateFlapMs = 2000;

// How long one read of the gate stands for. AGENTS.md's ~0.1s.
constexpr std::uint64_t kGateHoldMs = 100;

}  // namespace

Verdict Evaluate(std::uintptr_t controller) {
    // Held for 100ms. The injected caller is the funnel for CalcSceneView and
    // GetProjectionData both, so it is entered two or three times per rendered
    // frame and this would otherwise read the gate 300-400 times a second.
    // Menus, loading and pauses last orders of magnitude longer than the hold,
    // so nothing the gate exists to catch is missed.
    // Keyed on the controller as well as the clock: a map transition rebuilds
    // the controller, and a verdict read off the old one says nothing about the
    // new one however recently it was taken.
    static std::uint64_t s_stamp = 0;
    static std::uintptr_t s_controller = 0;
    static Verdict s_cached;
    static bool s_have = false;
    const std::uint64_t now = GetTickCount64();
    if (s_have && controller == s_controller && now - s_stamp < kGateHoldMs)
        return s_cached;

    // A profile without this offset never activates - build_registry treats it
    // as incomplete and the mod stays dormant - so there is no underived-gate
    // case to answer here, and no frame on which tracking runs ungated.
    Verdict v;
    std::uint32_t bits = 0;
    if (!ue::SafeReadU32(controller + Offsets().kShowMouseCursorOffset, bits)) {
        // A controller that will not read is not a controller. Fail closed:
        // holding the last frame's view is the safe answer, and an unreadable
        // frame must never be the one that turns tracking on.
        v.InGameplay = false;
        v.GateKnown = false;
        s_stamp = now;
        s_controller = controller;
        s_cached = v;
        s_have = true;
        return v;
    }
    v.GateKnown = true;
    v.InGameplay = (bits & Offsets().kShowMouseCursorMask) == 0;
    v.InConversation = !v.InGameplay && conversation_state::Active();
    s_stamp = now;
    s_controller = controller;
    s_cached = v;
    s_have = true;
    return v;
}

void LogTransitions(const Verdict& v) {
    if (g_haveLast && v.InGameplay == g_lastInGameplay && v.GateKnown == g_lastKnown
        && v.InConversation == g_lastConversation)
        return;
    const std::uint64_t now = GetTickCount64();
    if (g_haveLast && now - g_lastLineMs < kGateFlapMs) return;
    g_haveLast = true;
    g_lastInGameplay = v.InGameplay;
    g_lastKnown = v.GateKnown;
    g_lastConversation = v.InConversation;
    g_lastLineMs = now;
    Log::Line("gameplay-gate: %s (cursor flag %s)",
        v.InGameplay       ? "gameplay - tracking allowed"
        : v.InConversation ? "conversation - tracking allowed, crosshair parked"
                           : "not gameplay - tracking suppressed, view holds still",
        v.GateKnown ? "read" : "NOT readable");
}

}  // namespace tow_ht::game_state

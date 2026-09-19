// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "view_hook_diag.h"

#include <atomic>
#include <mutex>
#include <unordered_map>

#include <windows.h>

#include "ads.h"
#include "ads_state.h"
#include "aim_projection.h"
#include "camera_fov.h"
#include "lean_trace.h"
#include "logging.h"
#include "reticle_mover.h"
#include "ue_reflect.h"
#include "widget_probe.h"

namespace tow_ht::view_hook::diag {

namespace {

using cameraunlock::unreal::FRotator;

// ---- diagnostics cadence -------------------------------------------------
constexpr std::uint64_t kHeartbeatMs = 30000;
constexpr std::uint64_t kTrackerFlapMs = 2000;
constexpr std::uint64_t kPoseDetailMs = 2000;
constexpr int           kPoseDetailLines = 20;
constexpr std::uint64_t kCallerSummaryEvery = 1800;

// How often a state that is not transitioning is re-stated, so a line that
// never came can be told from one written at startup and scrolled away.
constexpr std::uint64_t kStateSampleMs = 60000;

// How long the widget probe keeps looking, and how often. The HUD is not built
// on the first frames, so a single early pass would list nothing.
constexpr std::uint64_t kWidgetProbeMs = 5000;

std::mutex g_callerMutex;
std::unordered_map<std::uintptr_t, std::uint64_t> g_callerCounts;
std::uint64_t g_callerLastSummary = 0;

// ---- heartbeat field words -----------------------------------------------
//
// Each answers one column of the heartbeat. Named functions rather than nested
// ternaries in the argument list because the ORDER of the tests is the whole
// content: "FAILED" has to beat "resolving", and an unproved layout has to beat
// both, or the line names a symptom instead of its cause.

const char* ReticleWord() {
    if (ReticleMover::TargetCount() == 0) return "no targets configured";
    return ReticleMover::Holding() ? "holding" : "searching";
}

// Ready() resolves, which walks the FField chain, so it is asked only once the
// layout has been proved - the same gate the frame path applies. Reporting the
// unproved state instead is also the more useful answer: it is the reason every
// reflection-backed feature is standing down.
const char* AimTraceWord(bool layoutOk) {
    if (!layoutOk) return "layout unproved";
    if (aim_trace::Failed()) return "FAILED";
    if (aim_trace::DispatchFailing()) return "FAULTING";
    return aim_trace::Ready() ? "ready" : "resolving";
}

const char* LeanClampWord(bool collisionEnabled, bool layoutOk) {
    if (!collisionEnabled) return "off";
    if (!layoutOk) return "layout unproved";
    if (lean_trace::Failed()) return "FAILED";
    return lean_trace::Ready() ? "ready" : "resolving";
}

}  // namespace

void NoteHookThread() {
    static std::atomic<unsigned long> s_first{0};
    static std::atomic<bool> s_said{false};
    const unsigned long id = ::GetCurrentThreadId();
    unsigned long none = 0;
    if (s_first.compare_exchange_strong(none, id, std::memory_order_relaxed)) return;
    if (none == id) return;
    if (s_said.exchange(true, std::memory_order_relaxed)) return;
    Log::Line("WARN: GetPlayerViewPoint was entered from thread %lu as well as %lu. "
              "The frame walk assumes one thread and shares unsynchronised state "
              "across it - the frame clock, the lean allowance, the pawn-offset "
              "latch and the trace parameter frames. Please report this log.",
        id, none);
}

void CountCaller(std::uintptr_t retRva, std::uint64_t call) {
    // One acquisition, and the summary counter inside it, so the count and the
    // interval cannot disagree about whose turn it is to print the table.
    std::lock_guard<std::mutex> lk(g_callerMutex);
    ++g_callerCounts[retRva];
    if (call - g_callerLastSummary < kCallerSummaryEvery) return;
    g_callerLastSummary = call;
    Log::Line("caller-summary @%llu calls: %zu unique return RVAs:",
        static_cast<unsigned long long>(call), g_callerCounts.size());
    for (const auto& kv : g_callerCounts)
        Log::Line("  ret RVA 0x%08llx  count=%llu",
            static_cast<unsigned long long>(kv.first),
            static_cast<unsigned long long>(kv.second));
}

void Heartbeat(const HeartbeatInputs& in) {
    static std::atomic<bool> s_first{true};
    static std::atomic<std::uint64_t> s_last{0};
    const std::uint64_t now = GetTickCount64();
    const bool first = s_first.exchange(false, std::memory_order_relaxed);
    if (!first && now - s_last.load(std::memory_order_relaxed) < kHeartbeatMs) return;
    s_last.store(now, std::memory_order_relaxed);

    float hy = 0.0f, hp = 0.0f, hr = 0.0f;
    const bool data = in.Tracking && in.Tracking->GetRotation(hy, hp, hr);
    const bool layoutOk = ue_reflect::ValidateLayout();
    Log::Line("heartbeat hook=%llu retRVA=0x%08llx enabled=%s udpData=%s "
              "raw=(Y=%.2f P=%.2f R=%.2f) gameplay=%s%s tracking=%s adsMode=%s "
              "sights=%s adsProbe=%s fov=%.1f base=%.1f zoom=%.3f "
              "yawMode=%s injectMode=%d reticle=%s aimTrace=%s leanClamp=%s%s",
        static_cast<unsigned long long>(in.Call),
        static_cast<unsigned long long>(in.RetRva),
        in.TrackingEnabled ? "ON" : "OFF",
        data ? "YES" : "NO", hy, hp, hr,
        in.Gate.InGameplay ? "yes" : "no", in.Gate.GateKnown ? "" : " (gate unreadable)",
        Reason(in.State.verdict), AdsModeValue(GetAdsMode()),
        in.State.aiming ? "up" : "down",
        ads_state::Failed() ? "FAILED" : "ok",
        camera_fov::GameFov(), camera_fov::BaseFov(), camera_fov::ZoomFactor(),
        in.WorldSpaceYaw ? "world" : "local", in.InjectMode,
        ReticleWord(), AimTraceWord(layoutOk),
        LeanClampWord(in.CollisionEnabled, layoutOk),
        in.LeanInContact ? " IN-CONTACT" : "");
}

void LogDecouplingSample(bool poseLog, bool trackingEnabled, std::uint64_t call,
                         std::uintptr_t retRva, const FRotator& out) {
    if (!poseLog) return;
    if (!trackingEnabled) return;
    static std::atomic<std::uint64_t> s_last{0};
    const std::uint64_t now = GetTickCount64();
    if (now - s_last.load(std::memory_order_relaxed) < kPoseDetailMs) return;
    s_last.store(now, std::memory_order_relaxed);
    Log::Line("decoupling: caller retRVA=0x%08llx (not injected) left with "
              "(Y=%.2f P=%.2f R=%.2f) at hook #%llu",
        static_cast<unsigned long long>(retRva), out.Yaw, out.Pitch, out.Roll,
        static_cast<unsigned long long>(call));
}

void LogPoseDetail(bool uncapped, const PoseSample& s) {
    static std::atomic<std::uint64_t> s_lastLine{0};
    static std::atomic<int> s_lines{0};
    // The cap is what keeps a shipped log to a few hundred KB an hour; a
    // measuring session wants the line for as long as it runs, which is what
    // [Dev] PoseLog lifts it for.
    if (!uncapped && s_lines.load(std::memory_order_relaxed) >= kPoseDetailLines) return;
    const std::uint64_t now = GetTickCount64();
    if (s.Call != 1 && now - s_lastLine.load(std::memory_order_relaxed) < kPoseDetailMs)
        return;
    s_lastLine.store(now, std::memory_order_relaxed);
    s_lines.fetch_add(1, std::memory_order_relaxed);

    // The aim point and the pixel offset ride on this line rather than on the
    // reticle's own, which is written only when the push actually moves: a head
    // held still leaves that line describing a frame seconds old, and a
    // measurement read off it is then a measurement of the wrong frame. This one
    // is written from the injected caller on a plain interval, so every line
    // describes the frame beside it.
    //
    // The aim point is in world coordinates because that is what the
    // opposite-lean gate is read from: leaning left and right must not move it,
    // and only the world point says so - the pixel offset is supposed to move.
    float retDx = 0.0f, retDy = 0.0f;
    const bool retValid = AimProjection::GetScreenOffset(retDx, retDy);
    Log::Line("hook #%llu retRVA=0x%08llx clean=(Y=%.2f P=%.2f R=%.2f) "
              "tracker=(Y=%.2f P=%.2f R=%.2f) result=(Y=%.2f P=%.2f R=%.2f) "
              "headOff_m=(x%.3f y%.3f z%.3f) posOff_ue=(%.1f,%.1f,%.1f) zoom=%.3f "
              "aim=%s aimPoint=(%.1f,%.1f,%.1f) aimDist=%.1fcm reticlePx=%s(%.1f,%.1f)",
        static_cast<unsigned long long>(s.Call),
        static_cast<unsigned long long>(s.RetRva),
        s.Clean.Yaw, s.Clean.Pitch, s.Clean.Roll, s.Yaw, s.Pitch, s.Roll,
        s.Result.Yaw, s.Result.Pitch, s.Result.Roll,
        s.OffsetX, s.OffsetY, s.OffsetZ,
        s.Applied.X, s.Applied.Y, s.Applied.Z, s.Zoom,
        !s.Aim.Valid ? "unavailable" : (s.Aim.Hit ? "hit" : "no-hit"),
        s.Aim.Point.X, s.Aim.Point.Y, s.Aim.Point.Z, s.Aim.Distance,
        retValid ? "" : "INVALID", retDx, retDy);
}

void LogLeanClampState(bool inContact, bool queryFailed) {
    static bool s_haveLast = false;
    static bool s_lastContact = false;
    static bool s_lastFailed = false;
    static std::uint64_t s_lastSample = 0;
    const std::uint64_t now = GetTickCount64();
    const bool changed =
        !s_haveLast || inContact != s_lastContact || queryFailed != s_lastFailed;
    if (!changed && now - s_lastSample < kStateSampleMs) return;
    // A change that arrives too soon after the last line is deferred rather than
    // written, the same way the tracker line is: an eye sitting against a
    // surface whose normal is square to the lean can flip contact on tracker
    // noise, and this runs on the render caller, so an undeferred flap is an
    // unbuffered locked write a couple of hundred times a second. The state it
    // settles on still gets written.
    if (changed && s_haveLast && now - s_lastSample < kTrackerFlapMs) return;
    s_haveLast = true;
    s_lastContact = inContact;
    s_lastFailed = queryFailed;
    s_lastSample = now;
    Log::Line("lean-clamp: contact=%s query=%s", inContact ? "yes" : "no",
        queryFailed ? "FAILED (lean unclamped)" : "ok");
}

void LogClampUnavailable() {
    static std::uint64_t s_lastSample = 0;
    const std::uint64_t now = GetTickCount64();
    if (s_lastSample != 0 && now - s_lastSample < kStateSampleMs) return;
    s_lastSample = now;
    Log::Line("lean-clamp: [Collision] Enabled is set but the engine's reflection "
              "layout is not proved on this build, so the sweep cannot run and the "
              "lean is UNCLAMPED");
}

void LogTrackerConnection(bool havePose) {
    static bool s_have = false;
    static bool s_known = false;
    static std::uint64_t s_lastLine = 0;
    if (s_known && havePose == s_have) return;
    // A tracker sending right on the 500ms freshness edge flips this twice a
    // second, so a change that arrives too soon after the last line is deferred
    // rather than dropped - the state it settles on still gets written.
    const std::uint64_t now = GetTickCount64();
    if (s_known && now - s_lastLine < kTrackerFlapMs) return;
    s_known = true;
    s_have = havePose;
    s_lastLine = now;
    Log::Line("tracker: %s", havePose
        ? "pose data flowing"
        : "no pose data - the tracker has stopped sending, or is not running yet");
}

void RunWidgetProbe(bool inGameplay, const char* outerContains) {
    if (!inGameplay) return;
    static bool s_validated = false;
    static bool s_ok = false;
    static std::uint64_t s_lastPass = 0;
    if (!s_validated) {
        s_validated = true;
        s_ok = WidgetProbe::ValidateGlobals();
    }
    if (!s_ok) return;
    const std::uint64_t now = GetTickCount64();
    if (s_lastPass != 0 && now - s_lastPass < kWidgetProbeMs) return;
    s_lastPass = now;
    WidgetProbe::DumpCandidates(outerContains);
}

}  // namespace tow_ht::view_hook::diag

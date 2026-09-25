// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "view_hook.h"

#include <atomic>
#include <cmath>

#include <windows.h>
#include <intrin.h>

#include "ads_gate.h"
#include "ads_pose.h"
#include "ads_state.h"
#include "aim_projection.h"
#include "aim_trace.h"
#include "builds/build_registry.h"
#include "camera_boundary.h"
#include "camera_fov.h"
#include "game_state.h"
#include "inject_mode.h"
#include "lean_trace.h"
#include "logging.h"
#include "reticle_mover.h"
#include "ue4_types.h"
#include "ue_reflect.h"
#include "view_hook_diag.h"

#include "cameraunlock/camera/lean_clamp.h"
#include "cameraunlock/camera/zoom_compensation.h"
#include "cameraunlock/hooks/hook_manager.h"
#include "cameraunlock/math/vec3.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/unreal/ue_math.h"
#include "cameraunlock/unreal/ue_runtime.h"

namespace tow_ht::view_hook {

namespace {

namespace ue = ::cameraunlock::unreal;

using ue::FRotator;
using cameraunlock::time::FrameClock;

// APlayerController::GetPlayerViewPoint(self, &OutLocation, &OutRotation).
using GetPlayerViewPoint_t =
    void(__fastcall*)(void* self, UeVector* outLocation, UeRotator* outRotation);

Dependencies g_deps{};

GetPlayerViewPoint_t g_orig = nullptr;
std::atomic<std::uint64_t> g_calls{0};

std::atomic<bool> g_trackingEnabled{true};
// true = world-space yaw (horizon-locked, FRotator addition); false = camera-
// local yaw (quaternion post-multiply, which leans on pitched turns).
std::atomic<bool> g_worldSpaceYaw{true};
std::atomic<int>  g_injectMode{inject::kFirstCaller};

// Ticked by the injected caller, which is NOT the same thing as once per
// rendered frame: that call site is the funnel UE uses for GetProjectionData as
// well as CalcSceneView, so a frame can enter it several times and the frame's
// dt arrives split across those entries. The pipeline is exponential and
// frame-rate independent, so a split dt integrates to the same place and the
// view is unaffected - but the aim cast and the lean sweep run once per entry
// rather than once per frame, which is cost paid for nothing.
FrameClock g_frameClock;

cameraunlock::camera::LeanClamp g_leanClamp;

// core's ScaleAngleForZoom is atan(tan(angle) * factor), which is the intended
// round trip only while |angle| < 90: the tangent runs away at 90 and comes back
// on the OPPOSITE side past it, so a 93 degree pose reads as -87 and the view
// snaps to the other shoulder - at zoom factor 1.0, where the function is
// supposed to be the identity. Nothing upstream bounds rotation: the processor
// clamps position only and has no rotation equivalent, so the pose crosses it
// whenever the tracker reports a yaw or pitch of 90 degrees or more.
//
// Past the limit the angle goes through unscaled: the compensation is worth less
// than the discontinuity, and at 89.9 the step between the scaled and unscaled
// value is under a fifth of a degree even at a 2x zoom, against the 178 degree
// flip it replaces. It is exactly zero at the 1.0 factor ordinary play runs at.
// A NaN fails the comparison and takes the same path, which neither creates nor
// removes one - nothing here is a NaN guard.
constexpr float kMaxZoomScaledAngle = 89.9f;

float ScaleForZoom(float angleDeg, float factor) {
    if (!(std::fabs(angleDeg) < kMaxZoomScaledAngle)) return angleDeg;
    return cameraunlock::camera::ScaleAngleForZoom(angleDeg, factor);
}

// True when no caller at all gets the pose in this mode: kNone, and every mode
// whose caller slot this profile left underived.
bool NothingInjects(int mode) {
    if (mode == inject::kAllCallers) return false;
    if (mode < inject::kFirstCaller || mode > static_cast<int>(inject::kCallerSlots))
        return true;
    return Offsets().kKnownCallerRvas[mode - 1] == 0;
}

bool ShouldInject(std::uintptr_t retRva, int mode) {
    if (mode == inject::kAllCallers) return true;
    if (mode < inject::kFirstCaller || mode > static_cast<int>(inject::kCallerSlots))
        return false;
    const auto rva = Offsets().kKnownCallerRvas[mode - 1];
    return rva != 0 && retRva == rva;
}

// [Dev] PoseLog: keep writing the per-frame pose and decoupling lines for a
// whole measuring session rather than stopping after the first few.
bool PoseLogging() { return g_deps.config->pose_log; }

std::uintptr_t ReturnRva(const void* returnAddress) {
    return reinterpret_cast<std::uintptr_t>(returnAddress) - ue::ModuleBase();
}

// AController::Pawn, resolved by name once and then read every frame. The trace
// and the sweep both need it: it is what they hand over as the world context so
// the query excludes the player's own capsule, which the eye sits inside.
std::size_t g_pawnOffset = 0;
bool g_pawnResolved = false;
bool g_pawnFailed = false;

std::uintptr_t PawnOf(std::uintptr_t controller) {
    if (g_pawnFailed || controller == 0) return 0;
    if (!g_pawnResolved) {
        const std::uintptr_t cls = ue_reflect::ClassOf(controller);
        if (!cls) return 0;
        ue_reflect::FieldInfo f;
        if (!ue_reflect::FindPropertyInChain(cls, "Pawn", f)) {
            g_pawnFailed = true;
            Log::Line("view-hook: AController::Pawn is not in the controller's "
                      "property chain, so neither the aim trace nor the lean sweep "
                      "can name an actor to ignore. Both stand down: the lean runs "
                      "unclamped, and the crosshair follows the aim direction while "
                      "the head is centred but is not marked at all once you lean.");
            return 0;
        }
        if (f.TypeName != "ObjectProperty" || f.Size != sizeof(std::uintptr_t)) {
            g_pawnFailed = true;
            Log::Line("view-hook: AController::Pawn is not a pointer-sized object "
                      "property");
            return 0;
        }
        g_pawnOffset = f.Offset;
        g_pawnResolved = true;
        Log::Line("view-hook: AController::Pawn at +0x%zx", g_pawnOffset);
    }
    std::uintptr_t pawn = 0;
    if (!ue::SafeReadPtr(controller + g_pawnOffset, pawn)) return 0;
    return pawn;
}

// Everything the mod does on a frame it is NOT allowed to move the view. The
// crosshair goes back where the game wants it and the clamp forgets the room it
// was in, so re-entering gameplay does not carry the last frame's wall in.
//
// The lean easing goes with them, so an aim interrupted by a menu or a dialogue
// does not come back with the lean still eased out.
void StandDown() {
    ads_pose::Reset();
    AimProjection::Invalidate();
    // Park rather than Tick: a suppressed frame pushes a zero offset through the
    // widgets already held, so the full object-table walk that re-finds them
    // would be a couple of milliseconds spent on an answer this frame cannot
    // use - every two seconds, for as long as the player sits in a menu.
    ReticleMover::Park();
    g_leanClamp.Reset();
}

// One rendered frame's worth of state, threaded between the steps below so each
// takes what it needs and returns what it produced. Everything in it comes from
// the engine or the session on THIS frame; nothing survives to the next.
struct Frame {
    std::uintptr_t RetRva = 0;
    std::uintptr_t Controller = 0;
    std::uintptr_t Pawn = 0;
    std::uint64_t  Call = 0;
    bool  ReflectionOk = false;
    float Dt = 0.0f;
    float Zoom = 1.0f;

    FRotator    Clean{};
    ue::FQuat4d CleanQ{0.0, 0.0, 0.0, 1.0};
    UeVector    CleanEye{0.0f, 0.0f, 0.0f};

    // The raw tracker pose, kept for the log line. What reaches the camera is
    // the lean-eased, zoom-scaled pose derived from it.
    float RawYaw = 0.0f, RawPitch = 0.0f, RawRoll = 0.0f;
    float RawOffX = 0.0f, RawOffY = 0.0f, RawOffZ = 0.0f;
};

// The field of view and the aspect model, read whether or not tracking is live:
// the reticle projection needs them, and having them on the first frames means
// the log carries the zoom factor without a tracker connected.
//
// The pawn is resolved here too - the aim trace and the lean sweep both hand it
// over as the world context, so the query excludes the player's own capsule.
// Nothing that walks the engine's reflection data runs until the profile's
// ReflectionLayout has been proved against a struct whose offsets are already
// known: a wrong layout does not fail loudly, it hands back offsets made of
// heap noise.
void ResolveFrameOptics(Frame& frame, UeVector* outLocation, UeRotator* outRotation) {
    frame.ReflectionOk = ue_reflect::ValidateLayout();
    if (frame.ReflectionOk) {
        // Both of these walk the FField chain, and both latch a permanent
        // failure. Run before the layout is proved, a wrong layout does not fail
        // loudly: it returns offsets made of heap noise, which either latches the
        // wrong diagnostic for the session or - worse - succeeds on a byte that
        // happens to be a valid enum and a float that happens to be a plausible
        // field of view, leaving every pose scaled by a constant nobody can see.
        camera_fov::ResolveAspectConstraint(frame.Controller);
        camera_fov::ResolveBaseFov();
        frame.Pawn = PawnOf(frame.Controller);
    }
    // Read uses the profile's pinned FMinimalViewInfo offsets rather than
    // reflection, so it stays outside the gate and the heartbeat carries a live
    // field of view on the first frames.
    AimProjection::SetFovDegrees(camera_fov::Read(outLocation, outRotation));
    lean_trace::SetPawn(frame.Pawn);
}

// What the sights do to this frame's pose, and the zoom factor to scale the
// result by.
ads_pose::Result ShapePose(Frame& frame, const TrackingState& state) {
    ads_pose::HeadPose absolute;
    absolute.yaw   = frame.RawYaw;
    absolute.pitch = frame.RawPitch;
    absolute.roll  = frame.RawRoll;
    absolute.x     = frame.RawOffX;
    absolute.y     = frame.RawOffY;
    absolute.z     = frame.RawOffZ;
    const ads_pose::Result shaped = ads_pose::Advance(state, absolute, GetTickCount64());
    frame.Zoom = camera_fov::ZoomFactor();
    return shaped;
}

// Compose the head rotation onto the clean view and write it back, returning
// the rotation the frame will be drawn with.
//
// Yaw and pitch translate the image across the frame, so both are scaled for
// zoom; roll rotates it about the view axis by the same angle at every field of
// view, so roll is left alone.
FRotator ApplyRotation(const Frame& frame, const ads_pose::HeadPose& pose,
                       UeRotator* outRotation) {
    FRotator tracked = frame.Clean;
    camera_boundary::ApplyHeadPose(
        tracked,
        ScaleForZoom(pose.yaw, frame.Zoom),
        ScaleForZoom(pose.pitch, frame.Zoom),
        pose.roll,
        g_worldSpaceYaw.load(std::memory_order_relaxed));
    outRotation->Pitch = static_cast<float>(tracked.Pitch);
    outRotation->Yaw   = static_cast<float>(tracked.Yaw);
    outRotation->Roll  = static_cast<float>(tracked.Roll);
    return tracked;
}

// Move the eye by the lean, clamped against the level, and write it back.
// Returns the offset actually applied, which is what the pose line reports.
//
// Clamp, then apply. The sweep starts from the CLEAN eye - the position the
// game itself put the camera at - because clamping afterwards would mean
// reading back a position that is already inside the wall.
ue::FVector ApplyPosition(const Frame& frame, const ads_pose::HeadPose& pose,
                          UeVector* outLocation) {
    ue::FVector applied = camera_boundary::PositionOffset(
        frame.CleanQ, pose.x * frame.Zoom, pose.y * frame.Zoom, pose.z * frame.Zoom);

    const bool clampWanted = g_deps.config->collision_enabled;
    if (clampWanted && frame.ReflectionOk) {
        const cameraunlock::math::Vec3 eye{frame.CleanEye.X, frame.CleanEye.Y,
                                           frame.CleanEye.Z};
        const cameraunlock::math::Vec3 want{static_cast<float>(applied.X),
                                            static_cast<float>(applied.Y),
                                            static_cast<float>(applied.Z)};
        const cameraunlock::math::Vec3 got =
            g_leanClamp.Apply(eye, want, frame.Dt, &lean_trace::Query, nullptr);
        applied = ue::FVector{got.x, got.y, got.z};
        diag::LogLeanClampState(g_leanClamp.InContact(), g_leanClamp.LastQueryFailed());
    } else if (clampWanted) {
        // Asked for, and not running. The lean goes through at full magnitude,
        // so the allowance from whichever room it last ran in must not survive -
        // and it has to be said, because a clamp that has quietly stopped
        // clamping looks exactly like one that never engaged.
        g_leanClamp.Reset();
        diag::LogClampUnavailable();
    }

    outLocation->X = frame.CleanEye.X + static_cast<float>(applied.X);
    outLocation->Y = frame.CleanEye.Y + static_cast<float>(applied.Y);
    outLocation->Z = frame.CleanEye.Z + static_cast<float>(applied.Z);
    return applied;
}

// Where the shot lands, in the frame that is about to be drawn, and the game's
// own crosshair moved to meet it.
//
// The cast runs from the CLEAN eye along the CLEAN forward, because that is the
// ray the game's own weapon trace will use - it reads the same clean view point
// through a caller this gate rejected.
aim_trace::Result UpdateAimAndReticle(const Frame& frame, const ue::FQuat4d& trackedQ,
                                      UeVector trackedEye, bool leanApplied) {
    const ue::FVector cleanFwdD =
        ue::QuatRotateVec(frame.CleanQ, ue::FVector{1.0, 0.0, 0.0});
    const UeVector cleanFwd{static_cast<float>(cleanFwdD.X),
                            static_cast<float>(cleanFwdD.Y),
                            static_cast<float>(cleanFwdD.Z)};

    aim_trace::Result aim;
    if (frame.ReflectionOk)
        aim = aim_trace::Cast(frame.Pawn, frame.CleanEye, cleanFwd,
                              g_deps.config->aim_trace_distance);
    AimProjection::Update(trackedEye, trackedQ, cleanFwd, leanApplied, aim.Valid,
                          aim.Hit, aim.Point, true);
    ReticleMover::Tick();
    return aim;
}

// A conversation. The dialogue camera frames the speaker and the cursor is up for
// the responses, so there is no aim to keep and nothing for the crosshair to
// mark. The pose is not shaped by the sights, but it IS zoom compensated, the
// same as in gameplay: the dialogue camera cuts between framings, down to a
// close-up on the speaker's face, and without compensation a narrow one
// magnifies every head turn - at a 20 degree field of view against the 75
// degree base the same turn sweeps about four times as much of the screen. With
// it, a head turn moves the picture as far as it does in gameplay, whatever the
// shot.
void ApplyConversation(Frame& frame, UeVector* outLocation, UeRotator* outRotation) {
    ads_pose::Reset();
    AimProjection::Invalidate();
    ReticleMover::Park();

    const bool havePosition = g_deps.session->GetPositionOffset(
        frame.RawOffX, frame.RawOffY, frame.RawOffZ);
    ads_pose::HeadPose pose;
    pose.yaw   = frame.RawYaw;
    pose.pitch = frame.RawPitch;
    pose.roll  = frame.RawRoll;
    pose.x     = frame.RawOffX;
    pose.y     = frame.RawOffY;
    pose.z     = frame.RawOffZ;

    frame.Zoom = camera_fov::ZoomFactor();
    frame.CleanQ =
        ue::QuatFromEulerDeg(frame.Clean.Pitch, frame.Clean.Yaw, frame.Clean.Roll);
    const FRotator tracked = ApplyRotation(frame, pose, outRotation);

    ue::FVector applied{0.0, 0.0, 0.0};
    if (havePosition)
        applied = ApplyPosition(frame, pose, outLocation);
    else
        g_leanClamp.Reset();

    diag::LogPoseDetail(PoseLogging(),
                        diag::PoseSample{frame.Call, frame.RetRva, frame.Clean, tracked,
                                         frame.RawYaw, frame.RawPitch, frame.RawRoll,
                                         frame.RawOffX, frame.RawOffY, frame.RawOffZ,
                                         applied, frame.Zoom, aim_trace::Result{}});
}

void __fastcall Hook(void* self, UeVector* outLocation, UeRotator* outRotation) {
    Frame frame;
    frame.RetRva = ReturnRva(_ReturnAddress());
    frame.Controller = reinterpret_cast<std::uintptr_t>(self);

    g_orig(self, outLocation, outRotation);

    frame.Clean = FRotator{static_cast<double>(outRotation->Pitch),
                           static_cast<double>(outRotation->Yaw),
                           static_cast<double>(outRotation->Roll)};
    frame.CleanEye = *outLocation;

    diag::NoteHookThread();
    frame.Call = g_calls.fetch_add(1, std::memory_order_relaxed) + 1;
    const int mode = g_injectMode.load(std::memory_order_relaxed);
    if (mode == inject::kAllCallers) diag::CountCaller(frame.RetRva, frame.Call);

    // Decoupling: every caller but the render/projection one keeps the clean
    // mouse/pad view, and that is what holds the shot on the aim rather than on
    // the head. Whatever the game aims, traces and listens with asks for the
    // view point through one of these, so the gate is the whole mechanism -
    // [Dev] PoseLog prints what each rejected caller walked away with, next to
    // the pose the render caller was given on the same frame.
    if (!ShouldInject(frame.RetRva, mode)) {
        // With no caller injecting at all, nothing downstream ever reaches the
        // stand-down, and ReticleMover::Push has already written a render
        // translation into the game's own widgets that persists until something
        // writes it again. So the crosshair would sit marking a point the rounds
        // no longer go, for the rest of the session.
        //
        // Keyed on whether ANY caller injects, not on kNone alone: this profile
        // fills six caller slots of sixteen, ShouldInject rejects every caller
        // for a mode whose slot RVA is 0, and Ctrl+Shift+J walks the whole
        // eighteen-position cycle - so modes 7 to 16 land here too. Keyed on the
        // render caller so it runs once per entry to that one call site rather
        // than once per caller; that site is itself entered two or three times a
        // frame, being the funnel for CalcSceneView and GetProjectionData both.
        if (frame.RetRva == Offsets().kKnownCallerRvas[0] && NothingInjects(mode)) {
            StandDown();
        }
        diag::LogDecouplingSample(PoseLogging(),
                                  g_trackingEnabled.load(std::memory_order_relaxed),
                                  frame.Call, frame.RetRva, frame.Clean);
        return;
    }

    // Read here rather than at the top of the hook: the gate is a guarded read
    // of the controller, and only the injected caller has anything to do with
    // the answer.
    const game_state::Verdict gate = game_state::Evaluate(frame.Controller);
    game_state::LogTransitions(gate);
    if (g_deps.config->widget_dump)
        diag::RunWidgetProbe(gate.InGameplay, g_deps.config->widget_dump_outer.c_str());
    ResolveFrameOptics(frame, outLocation, outRotation);

    frame.Dt = g_frameClock.Tick();

    bool havePose = false;
    if (g_deps.session->Update(frame.Dt))
        havePose =
            g_deps.session->GetRotation(frame.RawYaw, frame.RawPitch, frame.RawRoll);
    diag::LogTrackerConnection(havePose);

    // Polled, never latched, and tested last in the walk so a menu, a
    // conversation or a dead tracker still names its own reason when both are
    // true at once.
    const bool aiming = ads_state::IsAimingDownSights(frame.Pawn);
    const TrackingState state = DecideTracking(
        gate, g_trackingEnabled.load(std::memory_order_relaxed), havePose, aiming);

    diag::HeartbeatInputs beat;
    beat.Call             = frame.Call;
    beat.RetRva           = frame.RetRva;
    beat.Gate             = gate;
    beat.State            = state;
    beat.Tracking         = g_deps.session;
    beat.TrackingEnabled  = g_trackingEnabled.load();
    beat.WorldSpaceYaw    = g_worldSpaceYaw.load();
    beat.InjectMode       = g_injectMode.load();
    beat.CollisionEnabled = g_deps.config->collision_enabled;
    beat.LeanInContact    = g_leanClamp.InContact();
    diag::Heartbeat(beat);

    if (state.verdict == TrackingVerdict::Conversation) {
        ApplyConversation(frame, outLocation, outRotation);
        return;
    }
    if (!PoseApplies(state.verdict)) {
        StandDown();
        return;
    }

    const bool havePosition = g_deps.session->GetPositionOffset(
        frame.RawOffX, frame.RawOffY, frame.RawOffZ);

    const ads_pose::Result shaped = ShapePose(frame, state);

    frame.CleanQ =
        ue::QuatFromEulerDeg(frame.Clean.Pitch, frame.Clean.Yaw, frame.Clean.Roll);
    const FRotator tracked = ApplyRotation(frame, shaped.Pose, outRotation);
    const ue::FQuat4d trackedQ =
        ue::QuatFromEulerDeg(tracked.Pitch, tracked.Yaw, tracked.Roll);

    ue::FVector applied{0.0, 0.0, 0.0};
    if (havePosition) {
        applied = ApplyPosition(frame, shaped.Pose, outLocation);
    } else {
        // A frame with no lean on it forgets the room the last one was in, the
        // same as a suppressed frame does. Rotation-only mode and a tracker
        // publishing rotation alone both land here, and an allowance carried
        // out of them would ration the first lean after position comes back
        // through the release ease.
        g_leanClamp.Reset();
    }

    // Whether the render eye ended up anywhere other than the clean one. With the
    // two coincident the crosshair needs no parallax term, so the projection can
    // use the clean direction and does not depend on the cast having answered.
    const bool leanApplied =
        applied.X != 0.0 || applied.Y != 0.0 || applied.Z != 0.0;
    const aim_trace::Result aim =
        UpdateAimAndReticle(frame, trackedQ, *outLocation, leanApplied);

    diag::LogPoseDetail(PoseLogging(),
                        diag::PoseSample{frame.Call, frame.RetRva, frame.Clean, tracked,
                                         frame.RawYaw, frame.RawPitch, frame.RawRoll,
                                         frame.RawOffX, frame.RawOffY, frame.RawOffZ,
                                         applied, frame.Zoom, aim});
}

}  // namespace

bool Install(const Dependencies& deps) {
    // Proved once here rather than re-tested on every frame by every step that
    // reads them. Both are set unconditionally by the only caller, so a null is
    // a wiring mistake, and the ten per-frame tests this replaces would have run
    // the mod silently degraded - no reticle, no collision, no session - instead
    // of saying so.
    if (!deps.config || !deps.session) {
        Log::Line("FATAL: view hook installed without a %s",
            deps.config ? "tracking session" : "config");
        return false;
    }
    g_deps = deps;
    {
        g_trackingEnabled.store(deps.config->enable_on_startup);
        g_worldSpaceYaw.store(deps.config->world_space_yaw);
        aim_trace::SetTraceChannel(deps.config->aim_trace_channel);
        lean_trace::SetRadius(deps.config->collision_margin);
        lean_trace::SetChannel(deps.config->collision_channel);
        cameraunlock::camera::LeanClampSettings ls;
        // The swept sphere's radius IS the standoff - the hit location comes
        // back as where the sphere's CENTRE stopped, already backed off the
        // surface - so the clamp's own skin must be zero or the standoff is
        // applied twice.
        ls.skin = 0.0f;
        ls.release_smoothing = deps.config->collision_release_smoothing;
        g_leanClamp.SetSettings(ls);
    }
    g_injectMode.store(Offsets().kDefaultInjectMode);

    auto& hm = cameraunlock::hooks::HookManager::Instance();
    if (auto s = hm.Initialize(); s != cameraunlock::hooks::HookStatus::Ok) {
        Log::Line("FATAL: MinHook init failed: %s",
            cameraunlock::hooks::HookStatusToString(s));
        return false;
    }
    void* target = reinterpret_cast<void*>(
        ue::ModuleBase() + Offsets().kGetPlayerViewPointRva);
    if (auto s = hm.CreateHook(target, reinterpret_cast<void*>(&Hook),
                               reinterpret_cast<void**>(&g_orig));
        s != cameraunlock::hooks::HookStatus::Ok) {
        Log::Line("FATAL: CreateHook(GetPlayerViewPoint) failed: %s",
            cameraunlock::hooks::HookStatusToString(s));
        return false;
    }
    if (auto s = hm.EnableHook(target); s != cameraunlock::hooks::HookStatus::Ok) {
        Log::Line("FATAL: EnableHook failed: %s",
            cameraunlock::hooks::HookStatusToString(s));
        return false;
    }
    Log::Line("GetPlayerViewPoint hooked at RVA 0x%08llx (inject mode %d, render "
              "caller retRVA 0x%08llx)",
        static_cast<unsigned long long>(Offsets().kGetPlayerViewPointRva),
        g_injectMode.load(),
        static_cast<unsigned long long>(Offsets().kKnownCallerRvas[0]));
    return true;
}

void SetTrackingEnabled(bool enabled) { g_trackingEnabled.store(enabled); }
bool TrackingEnabled() { return g_trackingEnabled.load(); }
void SetWorldSpaceYaw(bool worldSpace) { g_worldSpaceYaw.store(worldSpace); }
bool WorldSpaceYaw() { return g_worldSpaceYaw.load(); }
void SetInjectMode(int mode) { g_injectMode.store(mode); }
int  InjectMode() { return g_injectMode.load(); }

}  // namespace tow_ht::view_hook

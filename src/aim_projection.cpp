// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Clean-aim -> screen projection for the game's own crosshair.
//
// Basis-to-basis, not a formula in yaw/pitch/roll: the view hook hands over the
// rotation it actually wrote into the view, so there is no second derivation of
// the composition to disagree with the first. A per-axis tangent formula would
// agree on single-axis poses and drift on combined ones, which is the bug that
// survives testing.
//
// The vector projected runs from the eye the FRAME IS DRAWN FROM to the point
// the shot lands on. That is what makes the mark hold under a lean at every
// range rather than at one: with the head centred the vector is just the clean
// forward, and with a lean it swings by exactly the parallax.

#include "aim_projection.h"

#include <atomic>
#include <cmath>
#include <mutex>

#include <windows.h>

#include "camera_fov.h"
#include "logging.h"

#include "cameraunlock/os/game_window.h"
#include "cameraunlock/rendering/aim_ndc_projection.h"

namespace tow_ht::AimProjection {

namespace {

namespace ue = ::cameraunlock::unreal;

// 0 until the hook pushes the first live value. Not a seeded default: a
// projection against a made-up field of view marks somewhere the rounds are not
// going, and no mark beats a wrong one.
std::atomic<float> g_fovDegrees{0.0f};

std::mutex g_mutex;
struct Frame {
    UeVector    Eye{0.0f, 0.0f, 0.0f};
    ue::FQuat4d Rotation{0.0, 0.0, 0.0, 1.0};
    UeVector    CleanDir{1.0f, 0.0f, 0.0f};
    bool        LeanApplied = false;
    bool        CastRan = false;
    bool        Hit = false;
    UeVector    HitPoint{0.0f, 0.0f, 0.0f};
};
Frame g_frame;
bool  g_active = false;

std::atomic<float> g_offsetX{0.0f};
std::atomic<float> g_offsetY{0.0f};
std::atomic<bool>  g_valid{false};
std::atomic<bool>  g_lastHadPoint{false};
std::atomic<bool>  g_lastCastRan{false};
std::atomic<float> g_lastDistance{0.0f};

// Said once per reason, because the honest answer to "why is the crosshair not
// moving" is one of two things and they need different fixes.
void ReportNoTangents(float fov, float aspect) {
    static std::atomic<bool> s_saidNoFov{false};
    static std::atomic<bool> s_saidAspect{false};
    if (!camera_fov::Plausible(fov)) {
        if (!s_saidNoFov.exchange(true, std::memory_order_relaxed))
            Log::Line("aim-projection: no usable field of view yet (%.1f degrees), so "
                      "nothing is being projected", fov);
        return;
    }
    if (!s_saidAspect.exchange(true, std::memory_order_relaxed))
        Log::Line("aim-projection: the display is %.2f:1 and the engine's aspect "
                  "constraint has not been read, so the two field-of-view models "
                  "disagree about where the aim lands. Nothing will be projected "
                  "until it resolves.", aspect);
}

void ToFloat3(const ue::FVector& v, float out[3]) {
    out[0] = static_cast<float>(v.X);
    out[1] = static_cast<float>(v.Y);
    out[2] = static_cast<float>(v.Z);
}

// The window the frame is drawn into. Held rather than looked up again every
// time: core's finder enumerates every top-level window in the session, and
// this runs on the render caller, so asking it per frame walks the whole
// desktop at the player's frame rate. A handle that has died or gone hidden -
// which is what UE does on a fullscreen-mode change - sends the search round
// again, which is also how the splash is left behind.
//
// Caller holds g_mutex.
HWND g_window = nullptr;

bool StillOurs(HWND wnd) {
    if (!IsWindow(wnd) || !IsWindowVisible(wnd)) return false;
    // A destroyed handle's value can come back attached to some other window,
    // this process's or another's, and the projection would then be scaled to
    // whatever that one's client area is. The finder filters on the owning
    // process, so the cached path has to as well.
    DWORD pid = 0;
    GetWindowThreadProcessId(wnd, &pid);
    return pid == GetCurrentProcessId();
}

HWND GameWindowLocked() {
    if (g_window && StillOurs(g_window)) return g_window;
    g_window = cameraunlock::os::FindGameWindow();
    return g_window;
}

// Nothing is marked this frame.
//
// LastHadImpactPoint and LastImpactDistance are deliberately NOT cleared here:
// they describe the last answer this file computed, and zeroing them would make
// them assert a wrong one - a cast that ran and hit a wall would report "no
// impact point, 0cm" on the frame the FOV read failed. The consumer prints them
// only alongside a valid offset, which is what keeps them honest.
//
// Caller holds g_mutex.
void InvalidateLocked() {
    g_valid.store(false, std::memory_order_relaxed);
}

// Caller holds g_mutex.
void RecomputeLocked() {
    if (!g_active) {
        InvalidateLocked();
        return;
    }
    RECT rc{};
    const HWND wnd = GameWindowLocked();
    if (!wnd || !GetClientRect(wnd, &rc) || rc.right <= 0 || rc.bottom <= 0) {
        InvalidateLocked();
        return;
    }

    const float w = static_cast<float>(rc.right);
    const float h = static_cast<float>(rc.bottom);

    const float fov = g_fovDegrees.load(std::memory_order_relaxed);
    float tanX = 0.0f, tanY = 0.0f;
    if (!camera_fov::HalfFieldTangents(camera_fov::AspectConstraint(), fov, w / h,
                                       tanX, tanY)) {
        ReportNoTangents(fov, w / h);
        InvalidateLocked();
        return;
    }

    // A cast that could not run says nothing about where the shot lands. That
    // only costs a mark once a lean has moved the render eye off the shot's own
    // ray, though: with the eye still on it, the clean direction is not a
    // fallback, it is the exact answer at every range. Tested AFTER the tangents
    // so the tangent diagnostic still gets its one shot on a build where both
    // are unavailable - the same reflection failure produces both.
    if (!g_frame.CastRan && g_frame.LeanApplied) {
        InvalidateLocked();
        return;
    }

    float fwd[3], right[3], up[3];
    ToFloat3(ue::QuatRotateVec(g_frame.Rotation, ue::FVector{1.0, 0.0, 0.0}), fwd);
    ToFloat3(ue::QuatRotateVec(g_frame.Rotation, ue::FVector{0.0, 1.0, 0.0}), right);
    ToFloat3(ue::QuatRotateVec(g_frame.Rotation, ue::FVector{0.0, 0.0, 1.0}), up);

    // A hit is a POINT and carries the parallax; a miss - or a frame with no
    // lean on it, where the two eyes coincide - is a point at infinity, for
    // which the clean direction is exact.
    //
    // The distance is held locally and published with the rest only once the
    // projection has succeeded: the two bails below would otherwise leave this
    // frame's number sitting next to the previous frame's answer on the
    // diagnostic line, which reads exactly like an arithmetic error when the
    // line is being used to check the arithmetic.
    float distance = 0.0f;
    float aim[3];
    if (g_frame.CastRan && g_frame.Hit) {
        aim[0] = g_frame.HitPoint.X - g_frame.Eye.X;
        aim[1] = g_frame.HitPoint.Y - g_frame.Eye.Y;
        aim[2] = g_frame.HitPoint.Z - g_frame.Eye.Z;
        const float len = std::sqrt(aim[0]*aim[0] + aim[1]*aim[1] + aim[2]*aim[2]);
        if (!(len > 1.0f)) {
            // The shot stops within a centimetre of the render eye. There is no
            // direction to project and the next frame will have a real one.
            InvalidateLocked();
            return;
        }
        aim[0] /= len; aim[1] /= len; aim[2] /= len;
        distance = len;
    } else {
        aim[0] = g_frame.CleanDir.X;
        aim[1] = g_frame.CleanDir.Y;
        aim[2] = g_frame.CleanDir.Z;
    }

    float ndcX = 0.0f, ndcY = 0.0f;
    if (!cameraunlock::rendering::ProjectAimToNdc(aim, fwd, right, up, tanX, tanY,
                                                  ndcX, ndcY)) {
        // The head has turned more than 90 degrees off the gun. There is no
        // screen position for the aim, so nothing is marked.
        InvalidateLocked();
        return;
    }

    // Clamp at the viewport edge. Without it an aim approaching 90 degrees off
    // axis sends the quotient into the thousands and the mark accelerates away
    // off screen instead of pinning to the edge the player should look towards.
    if (ndcX >  1.0f) ndcX =  1.0f;
    if (ndcX < -1.0f) ndcX = -1.0f;
    if (ndcY >  1.0f) ndcY =  1.0f;
    if (ndcY < -1.0f) ndcY = -1.0f;

    g_offsetX.store(ndcX * w * 0.5f, std::memory_order_relaxed);
    g_offsetY.store(-ndcY * h * 0.5f, std::memory_order_relaxed);
    g_lastHadPoint.store(g_frame.Hit, std::memory_order_relaxed);
    g_lastDistance.store(distance, std::memory_order_relaxed);
    g_valid.store(true, std::memory_order_relaxed);
}

}  // namespace

void Update(const UeVector& trackedEye, const ue::FQuat4d& trackedRotation,
            const UeVector& cleanAimDir, bool leanApplied, bool castRan, bool hit,
            const UeVector& hitPoint, bool active) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_frame.Eye = trackedEye;
    g_frame.Rotation = trackedRotation;
    g_frame.CleanDir = cleanAimDir;
    g_frame.LeanApplied = leanApplied;
    g_frame.CastRan = castRan;
    g_frame.Hit = hit;
    g_frame.HitPoint = hitPoint;
    g_active = active;
    g_lastCastRan.store(castRan, std::memory_order_relaxed);
    RecomputeLocked();
}

void Invalidate() {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_active = false;
    g_lastCastRan.store(false, std::memory_order_relaxed);
    RecomputeLocked();
}

void SetFovDegrees(float fovDegrees) {
    // A frame whose read failed pushes 0, which invalidates the projection on
    // the next recompute rather than leaving it running on the last good value
    // - the FOV moves with the sights, so a stale one is a moved crosshair.
    g_fovDegrees.store(camera_fov::Plausible(fovDegrees) ? fovDegrees : 0.0f,
                       std::memory_order_relaxed);
}

bool GetScreenOffset(float& dx, float& dy) {
    if (!g_valid.load(std::memory_order_relaxed)) return false;
    dx = g_offsetX.load(std::memory_order_relaxed);
    dy = g_offsetY.load(std::memory_order_relaxed);
    return true;
}

bool  LastHadImpactPoint() { return g_lastHadPoint.load(std::memory_order_relaxed); }
bool  LastCastRan() { return g_lastCastRan.load(std::memory_order_relaxed); }
float LastImpactDistance() { return g_lastDistance.load(std::memory_order_relaxed); }

}  // namespace tow_ht::AimProjection

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "camera_fov.h"

#include <atomic>
#include <cmath>

#include <string>

#include "builds/build_registry.h"
#include "kismet_frame.h"
#include "logging.h"
#include "ue_reflect.h"
#include "ue_vm.h"

#include "cameraunlock/camera/zoom_compensation.h"
#include "cameraunlock/memory/safe_memory.h"
#include "cameraunlock/unreal/ue_runtime.h"

// Where the field of view comes from, and where the offset goes.
//
// ULocalPlayer::GetViewPoint - the render/projection caller the view hook's
// caller gate injects for - fills the FMinimalViewInfo it was handed like this:
//
//     OutViewInfo      = CameraManager->GetCameraCacheView();
//     OutViewInfo.FOV  = CameraManager->GetFOVAngle();
//     PC->GetPlayerViewPoint(&OutViewInfo.Location,     <- this mod's hook
//                            &OutViewInfo.Rotation);
//
// So by the time the hook runs, the FOV this frame will be drawn with is
// already in the struct, one FVector past the Location pointer the hook was
// handed. This file only reads it. The game owns its own field of view, and
// what the mod wants the number for is the reticle projection and the zoom
// compensation.
namespace tow_ht::camera_fov {

namespace {

namespace ue = ::cameraunlock::unreal;

std::atomic<float> g_gameFov{0.0f};
std::atomic<int>   g_constraint{kConstraintUnknown};

bool g_constraintResolved = false;
bool g_constraintFailed = false;

std::atomic<float> g_baseFov{0.0f};
bool g_baseResolved = false;
bool g_baseFailed = false;
std::uintptr_t g_settingsObject = 0;
ue_reflect::FieldInfo g_customFovField;

// Said once, then never again: an ultrawide player needs to know why the
// reticle is not drawing, and a 16:9 player needs to know this changed nothing
// for them.
void GiveUpOnConstraint(const char* what) {
    if (g_constraintFailed) return;
    g_constraintFailed = true;
    Log::Line("fov: %s. Which axis the engine holds the field of view on cannot be "
              "read, and the two models it picks between name different axes - they "
              "disagree by the display's aspect ratio at every shape of display, "
              "16:9 included. So nothing is projected at all: no crosshair offset "
              "is applied.", what);
}

// The live UGameUserSettings, which is where the game's own Field of View
// slider ends up. Matched on the CLASS name rather than the object's, because
// the object is named after its class and so is the UClass itself - a name
// match alone would settle on whichever the table reached first. The
// class-default object is skipped for the same reason it is skipped when
// hunting widgets: it holds the shipped default, not the player's setting.
int g_settingsAttempts = 0;

std::uintptr_t FindGameUserSettings() {
    std::uintptr_t found = 0;
    ue::ForEachUObject([&](std::uintptr_t obj) -> bool {
        if (!ue::ContainsCI(ue::ClassName(obj), "GameUserSettings")) return false;
        const std::string name = ue::ObjectName(obj);
        if (ue::ContainsCI(name, "Default__")) return false;
        found = obj;
        return true;
    });
    return found;
}

// Said once. Without the un-zoomed reference there is no compensation, and a
// player who aims down a scope needs to know why tracking feels stronger there.
void GiveUpOnBase(const char* what) {
    if (g_baseFailed) return;
    g_baseFailed = true;
    Log::Line("fov: %s. Head tracking will not be scaled for zoom, so aiming down "
              "a scope will move the view further per degree of head movement "
              "than walking around does.", what);
}

// The live settings object and the offset of CustomFieldOfView on it. False is
// a frame to come back on unless GiveUpOnBase has been called, which the
// caller's own g_baseFailed check catches on the next pass.
//
// Both halves latch: the object is dropped and re-found when it is collected,
// while the field offset is a fact about the class and is resolved once.
bool ResolveSettingsField() {
    if (g_settingsObject != 0) return true;

    g_settingsObject = FindGameUserSettings();
    if (!g_settingsObject) {
        // FindGameUserSettings is the most expensive lookup in the mod - a whole
        // GUObjectArray walk that builds a class-name string for every object it
        // passes, with no early exit on a miss. Uncapped it would run four times
        // a second on the render thread for the rest of the session if a patch
        // renamed the class, and the only symptom would be the frame rate.
        if (++g_settingsAttempts >= kismet_frame::kMaxLookupAttempts)
            GiveUpOnBase("UGameUserSettings is not in this build's object table");
        return false;   // not constructed yet; retry
    }

    // Not guarded: FindGameUserSettings matched this object by reading its class
    // name microseconds ago on this thread, so ClassOf cannot fail here.
    const std::uintptr_t cls = ue_reflect::ClassOf(g_settingsObject);
    if (!ue_reflect::FindPropertyInChain(cls, "CustomFieldOfView", g_customFovField)) {
        ue_reflect::DumpProperties("GameUserSettings", cls);
        GiveUpOnBase("CustomFieldOfView is not in the game's user-settings "
                     "property chain - the table above is what it does carry");
        return false;
    }
    if (g_customFovField.Size != sizeof(float)) {
        GiveUpOnBase("CustomFieldOfView is not a float");
        return false;
    }
    return true;
}

// A frame whose field of view could not be read leaves nothing behind. Keeping
// the last good value would let the zoom compensation carry on scaling the pose
// by a field of view the game has stopped reporting, which is the guessed factor
// the whole feature exists to avoid - and it would do it silently, because a
// frozen factor reads exactly like a correct one. Clearing means ZoomFactor
// answers 1.0 and the reticle projection stands down together, on the same
// frame, for the same reason.
float Unreadable() {
    g_gameFov.store(0.0f, std::memory_order_relaxed);
    return 0.0f;
}

}  // namespace

float Read(const void* outLocation, const void* outRotation) {
    const auto& mvi = Offsets().MinimalViewInfoLayout;
    const auto locAddr = reinterpret_cast<std::uintptr_t>(outLocation);
    const auto rotAddr = reinterpret_cast<std::uintptr_t>(outRotation);
    // Unless the two pointers are exactly one FVector apart they are not fields
    // of one view info, and reading - let alone writing - past the first would
    // land in some other caller's stack frame.
    if (rotAddr - locAddr != mvi.kRotationStride) return Unreadable();

    float fov = 0.0f;
    if (!ue::SafeReadFloat(locAddr + mvi.kFovOffset, fov)) return Unreadable();
    if (!Plausible(fov)) return Unreadable();
    g_gameFov.store(fov, std::memory_order_relaxed);

    static std::atomic<bool> s_announced{false};
    if (!s_announced.exchange(true, std::memory_order_relaxed))
        Log::Line("fov: the game renders at %.1f degrees", fov);
    return fov;
}

void ResolveAspectConstraint(std::uintptr_t controller) {
    if (g_constraintResolved || g_constraintFailed) return;

    const std::uintptr_t controllerClass = ue_reflect::ClassOf(controller);
    if (!controllerClass) return;

    // APlayerController::Player, the UPlayer this controller belongs to. For the
    // controller the render caller asks for a view point, that is the
    // ULocalPlayer holding the viewport - which is the object that owns the
    // aspect constraint.
    ue_reflect::FieldInfo player;
    if (!ue_reflect::FindPropertyInChain(controllerClass, "Player", player)) {
        GiveUpOnConstraint("APlayerController::Player is not in the controller's "
                           "property chain");
        return;
    }
    if (player.TypeName != "ObjectProperty" || player.Size != sizeof(std::uintptr_t)) {
        GiveUpOnConstraint("APlayerController::Player is not a pointer-sized object "
                           "property");
        return;
    }

    std::uintptr_t localPlayer = 0;
    if (!ue::SafeReadPtr(controller + player.Offset, localPlayer) || !localPlayer) {
        // Not a failure worth giving up on: early frames run before the player
        // is attached, and the next frame will read it.
        return;
    }

    const std::uintptr_t playerClass = ue_reflect::ClassOf(localPlayer);
    if (!playerClass) return;

    ue_reflect::FieldInfo constraint;
    if (!ue_reflect::FindPropertyInChain(playerClass, "AspectRatioAxisConstraint",
                                         constraint)) {
        ue_reflect::DumpProperties("UPlayer", playerClass);
        GiveUpOnConstraint("AspectRatioAxisConstraint is not in the player's property "
                           "chain - the table above is what it does carry");
        return;
    }
    // TEnumAsByte<EAspectRatioAxisConstraint>. A wider field here is a different
    // property than the one this is reading, and taking its low byte would be a
    // coin toss between two projections.
    if (constraint.Size != 1) {
        GiveUpOnConstraint("AspectRatioAxisConstraint is not one byte wide");
        return;
    }

    std::uint8_t byte = 0;
    if (!cameraunlock::memory::SafeReadU8(localPlayer + constraint.Offset, byte)) return;

    const int value = static_cast<int>(byte);
    if (value != kMaintainYFOV && value != kMaintainXFOV && value != kMajorAxisFOV) {
        GiveUpOnConstraint("AspectRatioAxisConstraint holds a value that is not one of "
                           "the three the enum defines, so it is not where the engine's "
                           "reflection data says it is");
        return;
    }

    g_constraint.store(value, std::memory_order_relaxed);
    g_constraintResolved = true;
    Log::Line("fov: the engine holds the field of view on the %s axis "
              "(AspectRatioAxisConstraint=%s)",
              value == kMaintainYFOV ? "vertical" : value == kMaintainXFOV
                                                        ? "horizontal"
                                                        : "major",
              value == kMaintainYFOV   ? "MaintainYFOV"
              : value == kMaintainXFOV ? "MaintainXFOV"
                                       : "MajorAxisFOV");
}

int AspectConstraint() { return g_constraint.load(std::memory_order_relaxed); }

float GameFov() { return g_gameFov.load(std::memory_order_relaxed); }

void ResolveBaseFov() {
    if (g_baseFailed) return;

    // Re-read on an interval rather than once: the field-of-view slider is in
    // the game's own display settings and a player can move it mid-session, at
    // which point a base cached at startup would scale every frame after by the
    // ratio between the old setting and the new one.
    static ue_vm::ResolveRetry s_retry;
    if (!s_retry.Due()) return;

    if (!ResolveSettingsField()) return;

    float base = 0.0f;
    if (!ue::SafeReadFloat(g_settingsObject + g_customFovField.Offset, base)) {
        // The settings object was collected. Drop it and find the replacement
        // rather than reading a dead address every quarter second.
        //
        // This shares the budget with the object hunt above, and the budget is
        // only cleared once a read has actually SUCCEEDED - see below. Clearing
        // it on a bare find would make this cap dead: the object is re-found on
        // the next tick, the counter goes back to zero, and a field that never
        // reads costs a full object-table walk every quarter second forever,
        // which is what the cap is here to stop.
        g_settingsObject = 0;
        if (++g_settingsAttempts >= kismet_frame::kMaxLookupAttempts)
            GiveUpOnBase("the game's field-of-view setting cannot be read");
        return;
    }
    // The whole path worked, so the budget starts again.
    g_settingsAttempts = 0;
    if (!Plausible(base)) return;
    const float previous = g_baseFov.exchange(base, std::memory_order_relaxed);
    if (!g_baseResolved) {
        g_baseResolved = true;
        Log::Line("fov: the game's un-zoomed field of view is %.1f degrees "
                  "(UGameUserSettings::CustomFieldOfView at +0x%zx, the value behind "
                  "the game's own Field of View slider). Head tracking is scaled by "
                  "tan(live/2)/tan(%.1f/2) so a zoom does not magnify it; that factor "
                  "reads 1.0000 whenever the game is not zoomed.",
            base, g_customFovField.Offset, base);
    } else if (previous != base) {
        Log::Line("fov: the un-zoomed field of view changed %.1f -> %.1f degrees",
                  previous, base);
    }
}

float BaseFov() { return g_baseFov.load(std::memory_order_relaxed); }

float ZoomFactor() {
    const float live = g_gameFov.load(std::memory_order_relaxed);
    const float base = g_baseFov.load(std::memory_order_relaxed);
    const float factor = ZoomFactorFrom(live, base);
    // An unreadable field on either side means no compensation, never a guessed
    // one: a wrong factor is a sensitivity multiplier on every frame. The line
    // below waits for both, so its one shot is spent on real numbers rather than
    // on the zeroes of a frame before either had been read.
    if (!Plausible(live) || !Plausible(base)) return 1.0f;

    // Once, on the first frame the camera updates rather than the first frame a
    // pose arrives: a factor wrong by a constant reads exactly like a factor
    // that is right, so the terms have to be on a line a human can check, and
    // they have to be there without a tracker connected.
    static std::atomic<bool> s_announced{false};
    if (!s_announced.exchange(true, std::memory_order_relaxed)) {
        Log::Line("fov: zoom factor %.4f - live=%.2f deg (FMinimalViewInfo::FOV) "
                  "base=%.2f deg (UGameUserSettings::CustomFieldOfView), same scalar on "
                  "the same axis, tan(live/2)=%.4f tan(base/2)=%.4f. This reads 1.0000 "
                  "in ordinary gameplay; anything else means the two are not the same "
                  "quantity.",
            factor, live, base, std::tan(live * kHalfDegToRad),
            std::tan(base * kHalfDegToRad));
    }
    return factor;
}

}  // namespace tow_ht::camera_fov

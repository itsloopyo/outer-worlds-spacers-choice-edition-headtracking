// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cmath>
#include <cstdint>

#include "cameraunlock/camera/zoom_compensation.h"

// The field of view the frame is actually drawn with, and how it spreads over
// the two screen axes.
//
// Two things need it. The reticle projection divides by the half-field
// tangents, so a wrong field of view puts the mark a proportional distance from
// where the rounds go. And the game narrows its own field of view for sights,
// for Tactical Time Dilation and for cinematic pull-ins, which magnifies
// everything in the frame - head tracking included - so the pose is scaled by
// the ratio between the live field and the game's un-zoomed one before it is
// applied. Both read the same number from the same place, once per frame.
//
// The engine side lives in camera_fov.cpp. Everything above that line here is
// pure and header-only, so the tangent maths runs in tests with no game.
namespace tow_ht::camera_fov {

// What counts as a believable field of view, in degrees. Wide enough for every
// realistic FOV, so a value outside it is struct drift after a game patch or an
// uninitialised frame rather than a setting. The read, the offset and the
// projection all test against these.
constexpr float kMinDegrees = 10.0f;
constexpr float kMaxDegrees = 170.0f;

// Degrees to radians, halved, since every use here is of a half-angle.
constexpr float kHalfDegToRad = 3.14159265358979323846f / 360.0f;

// How the engine spreads one FOV scalar over the two axes. UE picks between
// these from ULocalPlayer::AspectRatioAxisConstraint and the viewport shape.
//
// The choice decides WHICH AXIS the scalar measures, so the two models do not
// agree at any aspect but a square one, and picking the wrong one at 16:9 is
// already a factor of 1.78. That is why an unread constraint projects nothing
// at all rather than falling back on either.
enum class AspectModel {
    // MaintainXFOV, and MajorAxisFOV on a viewport wider than it is tall. The
    // scalar IS the horizontal field; the vertical narrows as the display gets
    // wider (Vert-).
    HorizontalFixed,
    // MaintainYFOV. The scalar IS the vertical field; it stays put and the
    // horizontal grows with the display (Hor+). This is what an
    // ultrawide-friendly title sets.
    VerticalFixed,
};

// EAspectRatioAxisConstraint, in the order the shipping exe's own UEnum name
// table lists it. -1 is this mod's "not read yet", not an engine value.
inline constexpr int kConstraintUnknown = -1;
inline constexpr int kMaintainYFOV = 0;
inline constexpr int kMaintainXFOV = 1;
inline constexpr int kMajorAxisFOV = 2;

// UE's own rule, from the branch that picks the projection matrix multipliers:
// the horizontal field is the fixed one when the constraint says so outright,
// or when it says "major axis" and the viewport's major axis is the horizontal.
inline AspectModel ModelFor(int constraint, float viewportAspect) {
    if (constraint == kMaintainXFOV) return AspectModel::HorizontalFixed;
    if (constraint == kMajorAxisFOV && viewportAspect > 1.0f)
        return AspectModel::HorizontalFixed;
    return AspectModel::VerticalFixed;
}

// Phrased as a range test rather than its negation so a NaN, which fails every
// comparison, is rejected instead of passed through.
inline bool Plausible(float degrees) {
    return degrees >= kMinDegrees && degrees <= kMaxDegrees;
}

// The zoom factor for one pair of fields, with no engine state in it.
//
// Exactly 1.0 when the two are equal, which is the release gate: the live field
// and the base are the same scalar on the same axis, so ordinary gameplay must
// read 1.0000 and anything else means the two numbers are not the same quantity.
// 1.0 also whenever either is unreadable - no compensation, never a guessed one,
// because a wrong factor is a sensitivity multiplier on every frame.
inline float ZoomFactorFrom(float liveDegrees, float baseDegrees) {
    if (!Plausible(liveDegrees) || !Plausible(baseDegrees)) return 1.0f;
    const float factor = cameraunlock::camera::FovZoomFactor(
        std::tan(liveDegrees * kHalfDegToRad), std::tan(baseDegrees * kHalfDegToRad));
    if (!(factor > 0.0f) || !std::isfinite(factor)) return 1.0f;
    return factor;
}

// Half-field tangents for the frame: tan(fovX/2) and tan(fovY/2).
//
// This mirrors what UE actually builds. FMinimalViewInfo::CalculateProjectionMatrixGivenView
// hands ONE angle to FPerspectiveMatrix for both axes and separates them with a
// pair of multipliers, and FPerspectiveMatrix sets M[0][0] = MultFOVX/tan(half)
// and M[1][1] = MultFOVY/tan(half). Since an NDC axis divides by its own
// M element, tan(halfFovAxis) = tan(half)/MultAxis:
//
//   MaintainXFOV: MultX = 1,             MultY = SizeX/SizeY
//                 -> tanX = t,           tanY = t / viewportAspect
//   MaintainYFOV: MultX = SizeY/SizeX,   MultY = 1
//                 -> tanX = t * viewportAspect,  tanY = t
//
// The scalar therefore measures a DIFFERENT AXIS in each model, which is what
// "maintain X" and "maintain Y" mean. FMinimalViewInfo::AspectRatio plays no
// part: the engine ignores it unless bConstrainAspectRatio is set, so it is not
// read at all.
//
// False - project nothing - when the field of view is not usable or the aspect
// constraint has not been read. There is no aspect at which the two models are
// close enough to substitute for one another, so an unread constraint is
// missing information rather than a coin flip, and guessing draws a mark where
// the rounds are not going. No mark beats a wrong one.
inline bool HalfFieldTangents(int constraint, float fovDegrees,
                              float viewportAspect, float& tanX, float& tanY) {
    if (!Plausible(fovDegrees) || !(viewportAspect > 0.0f)) return false;
    if (constraint == kConstraintUnknown) return false;

    const float t = std::tan(fovDegrees * kHalfDegToRad);
    if (ModelFor(constraint, viewportAspect) == AspectModel::HorizontalFixed) {
        tanX = t;
        tanY = t / viewportAspect;
    } else {
        tanX = t * viewportAspect;
        tanY = t;
    }
    return tanX > 0.0f && tanY > 0.0f;
}

// ---- the engine side -----------------------------------------------------
//
// The order the render caller drives these in, once per engine frame, before
// anything to do with the head pose - the field of view is a separate feature
// and stays applied while tracking is toggled off:
//
//     camera_fov::ResolveAspectConstraint(controller);
//     camera_fov::ResolveBaseFov();
//     const float fov = camera_fov::Read(outLocation, outRotation);
//     AimProjection::SetFovDegrees(fov);
//
// Only for the caller the inject gate names: the stride test alone is not
// licence to treat whatever stack frame an unrecognised caller handed over as
// an FMinimalViewInfo.

// Read the FOV out of the FMinimalViewInfo the render caller is filling in.
// Returns the FOV the frame will be drawn with, or 0 when the two out-params
// are not fields of one view info, or what is there is not a believable angle.
// Either of those also clears GameFov(), so a frame the field could not be read
// from drops the zoom compensation back to 1.0 and stands the reticle
// projection down together, rather than leaving both running on the last frame
// that did read. Nothing is written back: the game owns its own field of view.
float Read(const void* outLocation, const void* outRotation);

// Ask the engine how it spreads that scalar over the axes, by reading
// ULocalPlayer::AspectRatioAxisConstraint off the controller's own player.
// Does the work once and answers from cache after that, so it is safe to call
// every frame. `controller` is the APlayerController the view query was made
// on, and the call must be on the game thread.
void ResolveAspectConstraint(std::uintptr_t controller);

// The EAspectRatioAxisConstraint the engine is using, or kConstraintUnknown
// until the read above has succeeded.
int AspectConstraint();

// The field of view the last frame was read with, for the heartbeat and the
// zoom compensation. Zero until one has been read, and zero again after a frame
// it could not be read from.
float GameFov();

// ---- the zoom base -------------------------------------------------------
//
// The game narrows its field of view for iron sights, for Tactical Time
// Dilation and for cinematic pull-ins, and a narrow field magnifies everything
// in the frame - head tracking included. Correcting that needs the un-zoomed
// field to measure against, and the reference has to be the game's OWN
// un-zoomed value rather than a constant or a running maximum: a constant is
// wrong for every player who moved the FOV slider, and a maximum tracked at
// runtime is raised for the session by one wide cinematic and quietly
// under-scales every frame after.
//
// UGameUserSettings::CustomFieldOfView is that value: the float the game's own
// Field of View slider writes, and the one it renders at whenever nothing is
// zooming. APlayerCameraManager::DefaultFOV is NOT it - this game leaves that
// at the engine's 90 while rendering the player's 75, so a factor built on it
// reads 0.767 in ordinary gameplay and shrinks every pose by a quarter.
//
// It is the same FOV scalar on the same axis as FMinimalViewInfo::FOV, read
// through the same reflection data, so the two cannot be paired across axes;
// the check that says so is that the factor reads 1.0000 in ordinary gameplay.

// Read the setting off the live UGameUserSettings. Re-reads on an interval, so
// moving the slider mid-session is picked up; game thread only.
void ResolveBaseFov();

// The un-zoomed field of view, or 0 until it has been read.
float BaseFov();

// How much to scale a translation - and, through ScaleAngleForZoom, a rotation
// - so its effect on the picture is what it would have been un-zoomed. Exactly
// 1.0 whenever the game is rendering its un-zoomed field, and 1.0 (no
// compensation at all) whenever either field is unreadable.
float ZoomFactor();

}  // namespace tow_ht::camera_fov

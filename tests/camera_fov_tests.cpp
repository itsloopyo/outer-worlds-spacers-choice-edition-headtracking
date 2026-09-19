// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// How one FOV scalar becomes the two half-field tangents the reticle
// projection divides by, and the zoom factor the pose is scaled with.
//
// UE spreads that scalar over the axes one of two ways, and the two are not
// approximations of each other: they name DIFFERENT AXES, so they disagree at
// every aspect but a square one and by 1.78 at plain 16:9. What this suite
// locks is each model against the projection matrix UE actually builds, the
// divergence between them, the refusal to guess when the engine's constraint
// has not been read, and the zoom factor reading exactly 1.0 in ordinary play.

#include "camera_fov.h"
#include "test_harness.h"

#include <limits>

namespace {

namespace fov = tow_ht::camera_fov;
using fov::AspectModel;

constexpr float k16x9 = 16.0f / 9.0f;
constexpr float k32x9 = 32.0f / 9.0f;

void TestPlausibleRejectsNonAnglesAndNaN() {
    CHECK(fov::Plausible(90.0f));
    CHECK(fov::Plausible(fov::kMinDegrees));
    CHECK(fov::Plausible(fov::kMaxDegrees));
    CHECK(!fov::Plausible(0.0f));
    CHECK(!fov::Plausible(fov::kMinDegrees - 0.1f));
    CHECK(!fov::Plausible(fov::kMaxDegrees + 0.1f));
    // Phrased as a range test so a NaN, which fails every comparison, is
    // rejected rather than passed through.
    CHECK_MSG(!fov::Plausible(std::numeric_limits<float>::quiet_NaN()),
              "a NaN field of view is not plausible");
}

// UE's own rule: the horizontal field is the fixed one when the constraint says
// so outright, or when it says major axis and the viewport's major axis is the
// horizontal.
void TestModelForFollowsTheEnginesBranch() {
    CHECK(fov::ModelFor(fov::kMaintainXFOV, k16x9) == AspectModel::HorizontalFixed);
    CHECK(fov::ModelFor(fov::kMaintainXFOV, 0.75f) == AspectModel::HorizontalFixed);
    CHECK(fov::ModelFor(fov::kMaintainYFOV, k16x9) == AspectModel::VerticalFixed);
    CHECK(fov::ModelFor(fov::kMajorAxisFOV, k16x9) == AspectModel::HorizontalFixed);
    CHECK_MSG(fov::ModelFor(fov::kMajorAxisFOV, 0.75f) == AspectModel::VerticalFixed,
              "major axis on a taller-than-wide viewport holds the vertical");
    CHECK_MSG(fov::ModelFor(fov::kMajorAxisFOV, 1.0f) == AspectModel::VerticalFixed,
              "a square viewport has no wider axis, so the vertical is held");
}

// MaintainXFOV sets XAxisMultiplier 1 and YAxisMultiplier SizeX/SizeY, so the
// scalar IS the horizontal half-field and the vertical narrows with the display.
void TestHorizontalFixedHoldsTheHorizontalField() {
    float tx = 0.0f, ty = 0.0f;
    CHECK(fov::HalfFieldTangents(fov::kMaintainXFOV, 90.0f, k16x9, tx, ty));
    CHECK_NEAR_MSG(tx, 1.0f, 1e-5, "tan(90/2) is 1, and the scalar is the horizontal field");
    CHECK_NEAR(ty, 1.0f / k16x9, 1e-5);

    CHECK(fov::HalfFieldTangents(fov::kMaintainXFOV, 90.0f, k32x9, tx, ty));
    CHECK_NEAR_MSG(tx, 1.0f, 1e-5, "the horizontal is held as the display widens");
    CHECK_NEAR_MSG(ty, 1.0f / k32x9, 1e-5, "and the vertical narrows with it - Vert-");
}

// MaintainYFOV swaps the multipliers: XAxisMultiplier SizeY/SizeX and
// YAxisMultiplier 1, so the scalar IS the VERTICAL half-field and the
// horizontal grows with the display. FMinimalViewInfo::AspectRatio is not a
// term - the engine ignores it unless bConstrainAspectRatio is set - and
// dividing by it here scaled both axes by 1.78 at the default reference,
// throwing the reticle most of a frame off in ordinary play.
void TestVerticalFixedHoldsTheVerticalField() {
    float tx = 0.0f, ty = 0.0f;
    CHECK(fov::HalfFieldTangents(fov::kMaintainYFOV, 90.0f, k16x9, tx, ty));
    CHECK_NEAR_MSG(ty, 1.0f, 1e-5, "the scalar is the vertical field");
    CHECK_NEAR_MSG(tx, k16x9, 1e-5, "and the horizontal grows with the display - Hor+");

    CHECK(fov::HalfFieldTangents(fov::kMaintainYFOV, 90.0f, k32x9, tx, ty));
    CHECK_NEAR_MSG(ty, 1.0f, 1e-5, "still the vertical, whatever the display");
    CHECK_NEAR(tx, k32x9, 1e-5);
}

// The two models name different axes, so they do not agree at 16:9 either -
// which is why an unread constraint cannot fall back on one of them.
void TestTheModelsDisagreeEvenAtTheCommonAspect() {
    float hx = 0.0f, hy = 0.0f, vx = 0.0f, vy = 0.0f;
    CHECK(fov::HalfFieldTangents(fov::kMaintainXFOV, 90.0f, k16x9, hx, hy));
    CHECK(fov::HalfFieldTangents(fov::kMaintainYFOV, 90.0f, k16x9, vx, vy));
    CHECK_NEAR_MSG(vx / hx, k16x9, 1e-4,
                   "picking the wrong model at 16:9 is already a factor of 1.78");
    CHECK_NEAR(vy / hy, k16x9, 1e-4);
}

// MajorAxisFOV picks its model from the viewport shape, so both branches are
// reachable from the one constraint value and both have to be right.
void TestMajorAxisFollowsTheViewportShape() {
    float tx = 0.0f, ty = 0.0f;
    CHECK(fov::HalfFieldTangents(fov::kMajorAxisFOV, 90.0f, k16x9, tx, ty));
    CHECK_NEAR_MSG(tx, 1.0f, 1e-5, "wider than tall: the scalar is the horizontal");
    CHECK_NEAR(ty, 1.0f / k16x9, 1e-5);

    const float portrait = 9.0f / 16.0f;
    CHECK(fov::HalfFieldTangents(fov::kMajorAxisFOV, 90.0f, portrait, tx, ty));
    CHECK_NEAR_MSG(ty, 1.0f, 1e-5, "taller than wide: the scalar is the vertical");
    CHECK_NEAR(tx, portrait, 1e-5);
}

// Refusing to guess. Without the constraint there is no answer to give, at any
// aspect, so nothing is projected.
void TestAnUnreadConstraintProjectsNothing() {
    float tx = 1.0f, ty = 1.0f;
    CHECK_MSG(!fov::HalfFieldTangents(fov::kConstraintUnknown, 90.0f, k16x9, tx, ty),
              "16:9 does not project without the constraint either");
    CHECK(!fov::HalfFieldTangents(fov::kConstraintUnknown, 90.0f, k32x9, tx, ty));
    CHECK_MSG(tx == 1.0f && ty == 1.0f, "a rejected read leaves the out-params alone");
}

void TestUnusableInputsProjectNothing() {
    float tx = 1.0f, ty = 1.0f;
    CHECK(!fov::HalfFieldTangents(fov::kMaintainXFOV, 0.0f, k16x9, tx, ty));
    CHECK(!fov::HalfFieldTangents(fov::kMaintainXFOV, 90.0f, 0.0f, tx, ty));
    CHECK(!fov::HalfFieldTangents(fov::kMaintainXFOV, 90.0f, -1.0f, tx, ty));
    CHECK_MSG(!fov::HalfFieldTangents(fov::kMaintainXFOV,
                                      std::numeric_limits<float>::quiet_NaN(), k16x9,
                                      tx, ty),
              "a NaN field of view projects nothing");
    CHECK_MSG(tx == 1.0f && ty == 1.0f, "a rejected read leaves the out-params alone");
}

// The release gate AGENTS.md names: ordinary gameplay reads 1.0000. Anything
// else means the live field and the base are not the same quantity, and the
// symptom is head tracking feeling weak everywhere rather than wrong anywhere.
void TestTheZoomFactorIsExactlyOneWhenNothingIsZooming() {
    CHECK(fov::ZoomFactorFrom(75.0f, 75.0f) == 1.0f);
    CHECK(fov::ZoomFactorFrom(90.0f, 90.0f) == 1.0f);
}

// A scope narrows the field, which magnifies the picture, so the pose is scaled
// DOWN to keep its screen displacement what it was un-zoomed.
void TestAZoomScalesThePoseDown() {
    const float f = fov::ZoomFactorFrom(45.0f, 90.0f);
    CHECK_MSG(f < 1.0f, "a narrower live field scales the pose down");
    // tan(22.5)/tan(45) = 0.4142.
    CHECK_NEAR(f, 0.41421356f, 1e-5);
    CHECK_MSG(fov::ZoomFactorFrom(110.0f, 90.0f) > 1.0f,
              "and a wider one scales it up");
}

// No compensation, never a guessed one. This is the regression the notes record
// having already been hit once, when Read stored the live field only on success
// and never cleared it, leaving the unreadable branch unreachable.
void TestAnUnreadableFieldMeansNoCompensation() {
    CHECK_MSG(fov::ZoomFactorFrom(0.0f, 75.0f) == 1.0f, "no live field, no compensation");
    CHECK_MSG(fov::ZoomFactorFrom(75.0f, 0.0f) == 1.0f, "no base, no compensation");
    CHECK(fov::ZoomFactorFrom(0.0f, 0.0f) == 1.0f);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_MSG(fov::ZoomFactorFrom(nan, 75.0f) == 1.0f, "a NaN field is not a field");
    CHECK(fov::ZoomFactorFrom(75.0f, nan) == 1.0f);
}

}  // namespace

int main() {
    TestPlausibleRejectsNonAnglesAndNaN();
    TestModelForFollowsTheEnginesBranch();
    TestHorizontalFixedHoldsTheHorizontalField();
    TestVerticalFixedHoldsTheVerticalField();
    TestTheModelsDisagreeEvenAtTheCommonAspect();
    TestMajorAxisFollowsTheViewportShape();
    TestAnUnreadConstraintProjectsNothing();
    TestUnusableInputsProjectNothing();
    TestTheZoomFactorIsExactlyOneWhenNothingIsZooming();
    TestAZoomScalesThePoseDown();
    TestAnUnreadableFieldMeansNoCompensation();
    return tow_test::Report();
}

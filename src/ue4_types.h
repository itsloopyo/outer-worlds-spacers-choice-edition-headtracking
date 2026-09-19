// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

// The engine's own POD types, at UE 4.27's widths.
//
// UE 4.27 predates Large World Coordinates: FVector, FRotator and FVector2D are
// floats. cameraunlock-core's ue::FVector / ue::FRotator are the UE5 double
// types and are used here only for the quaternion maths - every struct that
// crosses into engine memory (the GetPlayerViewPoint out-params, a Kismet trace
// parameter frame, a SetRenderTranslation argument) is one of these, and
// writing a core type into one of those slots would run 12 bytes off the end.
namespace tow_ht {

struct UeVector   { float X, Y, Z; };
struct UeRotator  { float Pitch, Yaw, Roll; };
struct UeVector2D { float X, Y; };

// The widths are the whole point of the file, and both the Kismet frame guard
// and every memcpy across the engine boundary take them from sizeof. An added
// member or a pack change would move them, the guard would compare the new size
// against the engine's and pass, and the copies would be wrong at both ends
// with nothing to catch it.
static_assert(sizeof(UeVector) == 12, "FVector is 3 floats before UE5's LWC");
static_assert(sizeof(UeRotator) == 12, "FRotator is 3 floats before UE5's LWC");
static_assert(sizeof(UeVector2D) == 8, "FVector2D is 2 floats before UE5's LWC");

}  // namespace tow_ht

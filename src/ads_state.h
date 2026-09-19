// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

// Are the sights up, as the game itself understands it?
//
// The Outer Worlds calls it fine aim, and it keeps the answer as a bool the
// first-person animation graph reads: `bIsFineAiming`, declared alongside
// `CurrentAimOffset`, `WeaponSway`, `PlayerCameraPitch` and `bIsDodging` on the
// animation instance driving the player's own FPVMesh. It is a statement about
// the sights rather than about magnification, so it is right for a weapon whose
// irons do not zoom as well as for a scoped long gun.
//
// The route to it is three named lookups and no pinned offsets:
// AIndianaPlayerCharacter::FPVMesh gives the first-person skeletal mesh,
// USkeletalMeshComponent::AnimScriptInstance gives the animation instance on it,
// and the engine's own reflection data gives the offset of the flag inside that.
// Asking by name means a patch that moves any of the three is followed rather
// than read as noise. Going through FPVMesh rather than hunting the animation
// instance in the object table is also what makes it the PLAYER's: a companion,
// an NPC and the character-creator preview all have animation instances too.
//
// POLLED, never latched. Enter and exit events fire unevenly - an aim released
// while firing, a state machine that transitions without an event - and a
// latched flag that misses an edge either strands the player in ADS behaviour or
// leaks hip-fire tracking into the aim. An unreadable frame answers "not
// aiming", because failing toward stock is the safe direction.
namespace tow_ht::ads_state {

// `pawn` is the APawn the view hook resolved off the controller this frame.
// Game thread only.
bool IsAimingDownSights(std::uintptr_t pawn);

// Whether the probe has stood down for the pawn class the player is in right
// now, for the heartbeat line. Scoped to a class rather than to the session: a
// pawn carrying no fine-aim flag is a menu or travel pawn, not a broken build,
// and possessing a real character re-resolves.
bool Failed();

}  // namespace tow_ht::ads_state

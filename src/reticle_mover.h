// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstddef>

namespace tow_ht::ReticleMover {

// Push the projected aim offset onto the game's own crosshair widget, so it
// sits where the shot lands instead of at the centre of the head-tracked
// picture. Called from the GetPlayerViewPoint hook on the render caller, which
// is a game-thread context - the UObject table and the script VM both require
// that.
void Tick();

// Park the crosshair back where the game puts it, for a frame the mod is not
// allowed to move the view. Pushes through the widgets already held and skips
// the object-table walk that re-finds them: a full GUObjectArray walk is the
// whole cost of this file, and a suppressed frame cannot use what it would
// find - it pushes a zero offset either way. Nothing is delayed by it, because
// the walk interval has long since come round by the time gameplay resumes.
void Park();

// Names of the widgets to move, comma-separated, from the config. Each entry is
// `Name` or `Name@OuterSubstring`. Empty leaves the crosshair alone entirely,
// which is what ships until the names have been read off a running game.
void SetTargets(const char* spec);

// Keep writing the per-push offset line instead of stopping after the first
// few, for a measuring session. [Dev] PoseLog.
void SetUncappedLog(bool on);

// Whether a live crosshair widget is currently held, and how many of the
// configured targets resolved, for the heartbeat.
bool Holding();
std::size_t TargetCount();

}  // namespace tow_ht::ReticleMover

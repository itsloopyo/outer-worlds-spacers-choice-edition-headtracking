// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <string>

#include "cameraunlock/config/config_concepts.g.h"
#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/math/smoothing_utils.h"
#include "cameraunlock/tracking/tracking_mode.h"

namespace tow_ht {

struct Config {
    int udp_port = 4242;
    bool enable_on_startup = true;

    // true: head yaw turns the view about the WORLD up-axis, so the horizon
    // stays level however far the mouse has pitched the camera. false: it turns
    // about the camera's own up-axis, which leans the horizon on a pitched turn.
    bool world_space_yaw = true;

    // The tracking mode at startup, as the pair the mode hotkey saves.
    bool rotation_enabled = true;
    bool position_enabled = true;

    // Centre the game window on the work area of its monitor at startup. Only a
    // windowed game is ever moved; fullscreen and borderless are left alone. A
    // player who deliberately positioned their window turns this off.
    bool center_window = true;

    // Smoothing is picked per connection from the packet source address: a
    // tracker running on this machine (loopback) uses local_smoothing, a remote
    // network device uses remote_smoothing. Both cover rotation and position.
    float local_smoothing = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
    float remote_smoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

    float limit_x = cameraunlock::PositionSettings{}.limit_x;
    float limit_y = cameraunlock::PositionSettings{}.limit_y;
    float limit_y_down = cameraunlock::PositionSettings{}.limit_y_down;
    float limit_z = cameraunlock::PositionSettings{}.limit_z;
    float limit_z_back = cameraunlock::PositionSettings{}.limit_z_back;

    // Lean collision. CollisionEnabled is global, so this is the schema's
    // built-in true that Defaults.ini falls back to. The sweep runs every
    // rendered frame the head is off centre, and trace channel 0 has not yet
    // been seen stopping on this game's geometry.
    bool collision_enabled = true;
    // Centimetres, the engine's unit. The swept sphere's radius, so it has to
    // exceed the camera's near clip distance to keep a wall from being cut away.
    float collision_margin = 12.0f;
    int collision_channel = 0;
    float collision_release_smoothing = 0.9f;

    std::string toggle_key =
        cameraunlock::config::schema::ConceptTraits<cameraunlock::config::schema::Concept::ToggleKey>::kCanonicalDefault;
    std::string cycle_tracking_mode_key = cameraunlock::config::schema::ConceptTraits<
        cameraunlock::config::schema::Concept::CycleTrackingModeKey>::kCanonicalDefault;
    std::string yaw_mode_key =
        cameraunlock::config::schema::ConceptTraits<cameraunlock::config::schema::Concept::YawModeKey>::kCanonicalDefault;

    // The aim cast that gives the crosshair its live depth. The channel is a
    // project setting rather than an engine constant, so it is a value to try
    // and read back out of the log. It is written into the cast's parameter
    // frame as one byte.
    int aim_trace_channel = 0;
    float aim_trace_distance = 20000.0f;   // UE units (cm)

    // Log every live UMG object whose name looks like a crosshair, with its
    // outer chain. How the names in ReticleMover's widget list are found.
    bool widget_dump = false;
    // With WidgetDump on, also list everything nested under an outer whose name
    // contains this - the whole widget tree of one HUD.
    std::string widget_dump_outer;

    // Keep logging the per-frame pose detail instead of stopping after the
    // first twenty lines. For measuring an axis or a reticle offset across a
    // whole session; it writes a line every couple of seconds.
    bool pose_log = false;

    // Overrides the build profile's kDefaultInjectMode when >= 0. Mode 0 logs a
    // summary of every distinct GetPlayerViewPoint return address, which is how
    // the render-path caller is (re-)identified after a patch.
    int inject_mode = -1;
    // Cycles the inject mode in game, for the same job.
    std::string inject_mode_key = "Ctrl+Shift+J";
};

}  // namespace tow_ht

// CameraUnlock.ini, next to the game exe, in cameraunlock-core's canonical
// config format. One ConfigOwner reads and writes it; nothing else in the mod
// touches it. HeadTracking.ini, the file the builds before it read, is imported
// once while CameraUnlock.ini is absent and is never written.
namespace tow_ht::config {

// The rows of CameraUnlock.ini.
cameraunlock::config::ConfigTable<Config> Table();

// What the renderer writes above the rows.
cameraunlock::config::RenderHeader Header();

// The owner's options for CameraUnlock.ini in `exe_dir`, with HeadTracking.ini
// beside it as the legacy file and Defaults.ini where `defaults` says.
cameraunlock::config::ConfigOwnerOptions<Config> OwnerOptions(const std::wstring& exe_dir,
                                                              cameraunlock::config::DefaultsFile defaults);

// Reads, imports or creates CameraUnlock.ini in `exe_dir`, logs what the owner
// reports, and returns the settings the session runs on. Call once, from the
// bootstrap thread, with the log open. `exe_dir` must be a full path, and
// `defaults` is DefaultsFile::PerUser() in the mod.
Config Load(const std::wstring& exe_dir, cameraunlock::config::DefaultsFile defaults);

// Save the value a hotkey has just applied. The session keeps it whether or not
// the save succeeds; a failed save is logged. Called from the hotkey thread.
void SaveWorldSpaceYaw(bool world_space_yaw);
void SaveTrackingMode(cameraunlock::TrackingMode mode);

}  // namespace tow_ht::config

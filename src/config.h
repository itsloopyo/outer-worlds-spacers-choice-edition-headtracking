// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <string>

#include "ads.h"

#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/math/smoothing_utils.h"

namespace tow_ht {

struct Config {
    int udp_port = 4242;
    bool enable_on_startup = true;

    // true: head yaw turns the view about the WORLD up-axis, so the horizon
    // stays level however far the mouse has pitched the camera. false: it turns
    // about the camera's own up-axis, which leans the horizon on a pitched
    // turn. Page Down switches it for the session; this is what the mod comes
    // up in.
    bool world_space_yaw = true;

    // Centre the game window on the work area of its monitor at startup. Only a
    // windowed game is ever moved; fullscreen and borderless are left alone. A
    // player who deliberately positioned their window turns this off.
    bool center_window = true;

    float yaw_sensitivity = 1.0f;
    float pitch_sensitivity = 1.0f;
    float roll_sensitivity = 1.0f;
    bool invert_yaw = false;
    bool invert_pitch = false;
    bool invert_roll = false;

    // Smoothing is picked per connection from the packet source address: a
    // tracker running on this machine (loopback) uses local_smoothing, a remote
    // network device uses remote_smoothing. Both cover rotation and position.
    float local_smoothing = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
    float remote_smoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

    bool position_enabled = true;
    float position_sensitivity_x = 1.0f;
    float position_sensitivity_y = 1.0f;
    float position_sensitivity_z = 1.0f;
    float limit_x = cameraunlock::PositionSettings{}.limit_x;
    float limit_y = cameraunlock::PositionSettings{}.limit_y;
    float limit_z = cameraunlock::PositionSettings{}.limit_z;
    float limit_z_back = cameraunlock::PositionSettings{}.limit_z_back;

    // Move the game's own crosshair to where the round lands. The widget names
    // are configuration because a HUD widget's name lives in a cooked Blueprint
    // asset rather than the exe, so it can only be read off a running game.
    bool reticle_enabled = true;
    std::string reticle_targets;

    // The aim cast that gives the crosshair its live depth. The channel is a
    // project setting rather than an engine constant, so it is a value to try
    // and read back out of the log.
    int aim_trace_channel = 0;
    float aim_trace_distance = 20000.0f;   // UE units (cm)

    // What head tracking does while the sights are up. Insert, or Ctrl+Shift+U,
    // cycles it in game and writes the new value back. An unrecognised string -
    // a typo, or a mode renamed since an older release wrote the file - lands on
    // the default rather than on whichever branch happens to be last, so a
    // player never ends up with head tracking through their sights that they did
    // not ask for.
    AdsMode ads_mode = kDefaultAdsMode;

    // Lean collision. Ships disabled: the sweep calls into the engine every
    // rendered frame the head is off centre, and an unverified trace channel
    // either blocks on nothing or blocks on everything.
    bool collision_enabled = false;
    float collision_radius = 12.0f;        // cm; must exceed the camera near clip
    int collision_channel = 0;
    float collision_release_smoothing = 0.9f;

    // Log every live UMG object whose name looks like a crosshair, with its
    // outer chain. How the names for reticle_targets are found.
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

    // Virtual-key code for the yaw-mode toggle, Page Down by default. The one
    // binding with an ini override: YawModeKey is part of the shared config
    // schema, the rest of the nav cluster is fixed across the fleet so the same
    // action sits on the same key in every mod.
    int yaw_mode_key = 0x22;
};

void config_load(const std::string& exe_dir, Config& out);
void config_write_default_if_missing(const std::string& exe_dir);

// Write the ADS mode back to the INI, leaving every other key and every comment
// in the file alone. The cycle key is the setting's other half, so a mode picked
// mid-firefight has to survive the next launch.
void config_save_ads_mode(AdsMode mode);

}  // namespace tow_ht

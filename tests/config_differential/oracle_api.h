// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <string>

// The oracle: the HeadTracking.ini reader of the newest published build, v0.1.0
// (1014f66, cameraunlock-core c480d8a), compiled only into this test.
// oracle/src/ holds that build's config.h, config.cpp and the headers they
// include from src/, and oracle/core/ the core headers they include that core
// has changed or removed since, all byte for byte as the tag has them
// (provenance in differential_tests.cpp). oracle_api.cpp compiles them with
// their namespaces renamed, so they link beside today's code.
namespace tow_oracle {

// tow_ht::Config as that build declared it, field for field.
struct PublishedConfig {
    int udp_port;
    bool enable_on_startup;
    bool world_space_yaw;
    bool center_window;
    float yaw_sensitivity;
    float pitch_sensitivity;
    float roll_sensitivity;
    bool invert_yaw;
    bool invert_pitch;
    bool invert_roll;
    float local_smoothing;
    float remote_smoothing;
    bool position_enabled;
    float position_sensitivity_x;
    float position_sensitivity_y;
    float position_sensitivity_z;
    float limit_x;
    float limit_y;
    float limit_z;
    float limit_z_back;
    bool reticle_enabled;
    std::string reticle_targets;
    int aim_trace_channel;
    float aim_trace_distance;
    // cameraunlock::ads::AdsMode at c480d8a: 0 paused, 1 marker, 2 tracked.
    int ads_mode;
    bool collision_enabled;
    float collision_radius;
    int collision_channel;
    float collision_release_smoothing;
    bool widget_dump;
    std::string widget_dump_outer;
    bool pose_log;
    int inject_mode;
    int yaw_mode_key;
};

// The published config_load on a default Config, as its bootstrap called it.
PublishedConfig Load(const std::string& exe_dir);

// The published config_write_default_if_missing: that build's first-run file.
void WriteDefaultIfMissing(const std::string& exe_dir);

}  // namespace tow_oracle

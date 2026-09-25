// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Built with tow_ht and cameraunlock renamed on the command line (CMakeLists.txt),
// so the published reader and the core code it calls are a separate copy from
// today's, and nothing here can resolve to a symbol of the mod under test.

#include "oracle_api.h"

#include "config.h"

namespace tow_oracle {

PublishedConfig Load(const std::string& exe_dir) {
    tow_ht::Config c;
    tow_ht::config_load(exe_dir, c);
    return PublishedConfig{
        c.udp_port,
        c.enable_on_startup,
        c.world_space_yaw,
        c.center_window,
        c.yaw_sensitivity,
        c.pitch_sensitivity,
        c.roll_sensitivity,
        c.invert_yaw,
        c.invert_pitch,
        c.invert_roll,
        c.local_smoothing,
        c.remote_smoothing,
        c.position_enabled,
        c.position_sensitivity_x,
        c.position_sensitivity_y,
        c.position_sensitivity_z,
        c.limit_x,
        c.limit_y,
        c.limit_z,
        c.limit_z_back,
        c.reticle_enabled,
        c.reticle_targets,
        c.aim_trace_channel,
        c.aim_trace_distance,
        static_cast<int>(c.ads_mode),
        c.collision_enabled,
        c.collision_radius,
        c.collision_channel,
        c.collision_release_smoothing,
        c.widget_dump,
        c.widget_dump_outer,
        c.pose_log,
        c.inject_mode,
        c.yaw_mode_key,
    };
}

void WriteDefaultIfMissing(const std::string& exe_dir) { tow_ht::config_write_default_if_missing(exe_dir); }

}  // namespace tow_oracle

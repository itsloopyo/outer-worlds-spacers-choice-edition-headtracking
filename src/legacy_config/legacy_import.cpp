// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Frozen. See legacy_import.h.

#include "legacy_import.h"

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>

#include "legacy_config.h"

#include "cameraunlock/input/key_bindings.h"

namespace tow_ht::legacy {

namespace {

namespace cfg = ::cameraunlock::config;
using ::cameraunlock::input::FormatKeyBindings;
using ::cameraunlock::input::KeyModifiers;

constexpr KeyModifiers kChord = KeyModifiers::kCtrl | KeyModifiers::kShift;

// The keys the frozen reader's build bound in code rather than in the file.
constexpr int kVkEnd = 0x23;
constexpr int kVkPageUp = 0x21;
constexpr int kVkY = 0x59;
constexpr int kVkG = 0x47;
constexpr int kVkH = 0x48;
constexpr int kVkJ = 0x4A;

constexpr const char* kIniName = "\\HeadTracking.ini";

cfg::ImportResult Run(const cfg::LegacyInput& input, tow_ht::Config& out) {
    // The frozen reader takes the folder and names the file itself.
    const std::string& path = input.ansi_path;
    const std::size_t name_length = std::strlen(kIniName);
    if (path.size() < name_length || _stricmp(path.c_str() + path.size() - name_length, kIniName) != 0) {
        throw std::invalid_argument("the legacy import reads HeadTracking.ini only, not " + path);
    }
    const std::size_t name_at = path.size() - name_length;
    // IniReader::Open's own test: without it the frozen reader ran on its defaults.
    const bool present = GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;

    Config read;
    Load(path.substr(0, name_at), read);

    std::vector<cfg::DroppedValue> dropped;

    out.udp_port = read.udp_port;
    out.enable_on_startup = read.enable_on_startup;
    out.world_space_yaw = read.world_space_yaw;
    out.center_window = read.center_window;
    // The reader turns a value that is not a finite number into its default, so
    // every float here is finite.
    out.local_smoothing = read.local_smoothing;
    out.remote_smoothing = read.remote_smoothing;
    // [Position] Enabled picked the startup mode and nothing else: the mode cycle
    // still reached both position modes.
    out.rotation_enabled = true;
    out.position_enabled = read.position_enabled;
    // The one vertical limit was applied both ways.
    out.limit_x = read.limit_x;
    out.limit_y = read.limit_y;
    out.limit_y_down = read.limit_y;
    out.limit_z = read.limit_z;
    out.limit_z_back = read.limit_z_back;

    // The lean sweep shipped switched off until it is verified in this game, so
    // it follows the mod's default (approved change follows_default).
    out.collision_enabled = tow_ht::Config{}.collision_enabled;
    if (read.collision_enabled != out.collision_enabled) {
        dropped.push_back({cfg::DropRule::FollowsDefault, "Collision", "Enabled",
                           read.collision_enabled ? "true" : "false"});
    }
    out.collision_margin = read.collision_radius;
    out.collision_channel = read.collision_channel;
    out.collision_release_smoothing = read.collision_release_smoothing;

    out.aim_trace_channel = read.aim_trace_channel;
    out.aim_trace_distance = read.aim_trace_distance;
    out.widget_dump = read.widget_dump;
    out.widget_dump_outer = read.widget_dump_outer;
    out.pose_log = read.pose_log;
    out.inject_mode = read.inject_mode;

    // End, Page Up, the Ctrl+Shift chords and the Ctrl+Shift+J inject chord were
    // bound in code; only the yaw key was in the file, and the reader keeps it
    // inside 0x01-0xFE.
    out.toggle_key = FormatKeyBindings({{KeyModifiers::kNone, kVkEnd}, {kChord, kVkY}});
    out.cycle_tracking_mode_key = FormatKeyBindings({{KeyModifiers::kNone, kVkPageUp}, {kChord, kVkG}});
    out.yaw_mode_key = FormatKeyBindings({{KeyModifiers::kNone, read.yaw_mode_key}, {kChord, kVkH}});
    out.inject_mode_key = FormatKeyBindings({{kChord, kVkJ}});

    // The game's crosshair now always follows the aim, over the widgets the mod
    // names in code (approved change reticle).
    if (!read.reticle_enabled) dropped.push_back({cfg::DropRule::Reticle, "Reticle", "Enabled", "false"});
    if (read.reticle_targets != kDefaultReticleTargets) {
        dropped.push_back({cfg::DropRule::Reticle, "Reticle", "Targets", read.reticle_targets});
    }

    // Every one of these shipped as identity, so nothing is folded into the
    // mod's axis code and a value the player changed is dropped.
    const Config shipped{};
    std::vector<cfg::PoseShapingValue> pose;
    cfg::LegacyPoseShaping(read.yaw_sensitivity, shipped.yaw_sensitivity, "Rotation", "YawSensitivity", pose,
                           dropped);
    cfg::LegacyPoseShaping(read.pitch_sensitivity, shipped.pitch_sensitivity, "Rotation", "PitchSensitivity", pose,
                           dropped);
    cfg::LegacyPoseShaping(read.roll_sensitivity, shipped.roll_sensitivity, "Rotation", "RollSensitivity", pose,
                           dropped);
    cfg::LegacyPoseShaping(read.invert_yaw, shipped.invert_yaw, "Rotation", "InvertYaw", pose, dropped);
    cfg::LegacyPoseShaping(read.invert_pitch, shipped.invert_pitch, "Rotation", "InvertPitch", pose, dropped);
    cfg::LegacyPoseShaping(read.invert_roll, shipped.invert_roll, "Rotation", "InvertRoll", pose, dropped);
    cfg::LegacyPoseShaping(read.position_sensitivity_x, shipped.position_sensitivity_x, "Position", "SensitivityX",
                           pose, dropped);
    cfg::LegacyPoseShaping(read.position_sensitivity_y, shipped.position_sensitivity_y, "Position", "SensitivityY",
                           pose, dropped);
    cfg::LegacyPoseShaping(read.position_sensitivity_z, shipped.position_sensitivity_z, "Position", "SensitivityZ",
                           pose, dropped);

    return present ? cfg::ImportResult::Imported(std::move(dropped), std::move(pose))
                   : cfg::ImportResult::Absent(std::move(dropped), std::move(pose));
}

}  // namespace

cfg::LegacyImport<tow_ht::Config> Import() {
    cfg::LegacyImport<tow_ht::Config> import;
    import.run = &Run;
    // Every key the frozen reader takes a value from. The retired [Rotation] and
    // [Position] Smoothing keys it only names in the log are not here: no value
    // of theirs reached anything, and the conversion logs each as not carried.
    import.keys = {
        {"Network", "UdpPort"},
        {"General", "EnableOnStartup"},
        {"General", "WorldSpaceYaw"},
        {"General", "CenterWindow"},
        {"Rotation", "YawSensitivity"},
        {"Rotation", "PitchSensitivity"},
        {"Rotation", "RollSensitivity"},
        {"Rotation", "InvertYaw"},
        {"Rotation", "InvertPitch"},
        {"Rotation", "InvertRoll"},
        {"Rotation", "LocalSmoothing"},
        {"Rotation", "RemoteSmoothing"},
        {"Position", "Enabled"},
        {"Position", "SensitivityX"},
        {"Position", "SensitivityY"},
        {"Position", "SensitivityZ"},
        {"Position", "LimitX"},
        {"Position", "LimitY"},
        {"Position", "LimitZ"},
        {"Position", "LimitZBack"},
        {"Reticle", "Enabled"},
        {"Reticle", "Targets"},
        {"Aim", "TraceChannel"},
        {"Aim", "MaxDistance"},
        {"Collision", "Enabled"},
        {"Collision", "Radius"},
        {"Collision", "Channel"},
        {"Collision", "ReleaseSmoothing"},
        {"Dev", "WidgetDump"},
        {"Dev", "WidgetDumpOuter"},
        {"Dev", "PoseLog"},
        {"Diag", "InjectMode"},
        {"Hotkeys", "YawModeKey"},
    };
    return import;
}

}  // namespace tow_ht::legacy

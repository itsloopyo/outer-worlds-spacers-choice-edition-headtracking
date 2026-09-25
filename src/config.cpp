// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "inject_mode.h"
#include "legacy_config/legacy_import.h"
#include "logging.h"

#include "cameraunlock/config/hotkey_codec.h"
#include "cameraunlock/config/value_codecs.h"

namespace tow_ht::config {

namespace {

namespace cfg = ::cameraunlock::config;
using cfg::schema::Concept;

constexpr const wchar_t* kIniName = L"HeadTracking.ini";

// data/games.json's display_name for outer-worlds-spacers-choice-edition.
constexpr const char* kDisplayName = "The Outer Worlds: Spacer's Choice Edition";

// The trace channel is written into the cast's parameter frame as one byte.
constexpr double kMaxTraceChannel = 255;
constexpr double kMinAimDistanceCm = 1.0;
constexpr double kMaxAimDistanceCm = 1000000.0;

std::unique_ptr<cfg::ConfigOwner<Config>> g_owner;

void Save(const char* rows, const std::function<void(Config&)>& change) {
    const cfg::ConfigSaveResult result = g_owner->Save(change);
    if (result.status == cfg::ConfigSaveStatus::Saved) return;
    Log::Line("config: %s %s: %s", rows, cfg::ConfigSaveStatusName(result.status), result.reason.c_str());
    for (const std::string& line : result.log) Log::Line("config: %s", line.c_str());
}

}  // namespace

cfg::ConfigTable<Config> Table() {
    cfg::ConfigTable<Config> table;
    table.Concept<Concept::UdpPort>(&Config::udp_port)
        .Concept<Concept::EnableOnStartup>(&Config::enable_on_startup)
        .Concept<Concept::WorldSpaceYaw>(&Config::world_space_yaw)
        .Writable()
        .Concept<Concept::RotationEnabled>(&Config::rotation_enabled)
        .Writable()
        .Local("General", "CenterWindow", &Config::center_window, cfg::BoolCodec(),
               "true: centre the game window on the work area of its monitor at startup. Only a\n"
               "windowed game is moved; fullscreen and borderless are left alone.")
        .Concept<Concept::LocalSmoothing>(&Config::local_smoothing)
        .Concept<Concept::RemoteSmoothing>(&Config::remote_smoothing)
        .Concept<Concept::PositionEnabled>(&Config::position_enabled)
        .Writable()
        .Concept<Concept::PositionLimitX>(&Config::limit_x)
        .Concept<Concept::PositionLimitY>(&Config::limit_y)
        .Concept<Concept::PositionLimitYDown>(&Config::limit_y_down)
        .Concept<Concept::PositionLimitZ>(&Config::limit_z)
        .Concept<Concept::PositionLimitZBack>(&Config::limit_z_back)
        .Concept<Concept::CollisionEnabled>(&Config::collision_enabled)
        .Concept<Concept::CollisionMargin>(&Config::collision_margin)
        .Comment("How far the view is held off a wall when you lean into it, in centimetres.")
        .Concept<Concept::CollisionChannel>(&Config::collision_channel)
        .Engine()
        .Concept<Concept::CollisionReleaseSmoothing>(&Config::collision_release_smoothing)
        .Concept<Concept::ToggleKey>(&Config::toggle_key)
        .Concept<Concept::CycleTrackingModeKey>(&Config::cycle_tracking_mode_key)
        .Concept<Concept::YawModeKey>(&Config::yaw_mode_key)
        .Local("Aim", "AimTraceChannel", &Config::aim_trace_channel, cfg::IntCodec<int>(),
               "Which of the game's collision channels the aim cast tests against, 0 to 255. The cast\n"
               "finds how far away the point you aim at is, so the crosshair sits where the shot lands.")
        .Range(0, kMaxTraceChannel)
        .Engine()
        .Local("Aim", "MaxDistance", &Config::aim_trace_distance, cfg::FloatingCodec<float>(),
               "How far the aim cast reaches, in centimetres. Past it the crosshair marks the aim\n"
               "direction instead of a point.")
        .Range(kMinAimDistanceCm, kMaxAimDistanceCm)
        .Local("Dev", "WidgetDump", &Config::widget_dump, cfg::BoolCodec(),
               "true: write every widget whose name looks like a crosshair to HeadTracking.log, with\n"
               "the objects it sits under. For finding the crosshair after a game patch.")
        .Local("Dev", "WidgetDumpOuter", &Config::widget_dump_outer, cfg::StringCodec(),
               "With WidgetDump on, also list every widget nested under an object whose name holds\n"
               "this text.")
        .Local("Dev", "PoseLog", &Config::pose_log, cfg::BoolCodec(),
               "true: keep writing the head pose to HeadTracking.log every two seconds, instead of\n"
               "stopping after the first twenty lines. For measuring.")
        .Local("Dev", "InjectMode", &Config::inject_mode, cfg::IntCodec<int>(),
               "Which of the game's view point callers is given the head pose, in place of the one\n"
               "this build picks. -1 keeps the build's choice. The others are for finding the render\n"
               "path after a game patch, and 0 hands every caller the head pose, which turns aim\n"
               "decoupling off.")
        .Range(-1, inject::kModeCount - 1)
        .Local("Dev", "InjectModeKey", &Config::inject_mode_key, cfg::HotkeyCodec(),
               "Steps through the inject modes in game, for the same job. The next start goes back\n"
               "to InjectMode.");
    return table;
}

cfg::RenderHeader Header() {
    cfg::RenderHeader header;
    header.display_name = kDisplayName;
    return header;
}

cfg::ConfigOwnerOptions<Config> OwnerOptions(const std::wstring& path) {
    cfg::ConfigOwnerOptions<Config> options;
    options.path = path;
    options.table = Table();
    options.import = legacy::Import();
    options.header = Header();
    return options;
}

Config Load(const std::wstring& exe_dir) {
    g_owner = std::make_unique<cfg::ConfigOwner<Config>>(OwnerOptions(exe_dir + L"\\" + kIniName));
    const cfg::ConfigLoadResult<Config> result = g_owner->Load();
    for (const std::string& line : result.log) Log::Line("config: %s", line.c_str());
    if (!result.reason.empty()) Log::Line("config: %s", result.reason.c_str());
    Log::Line("config: %s", cfg::ConfigLoadStatusName(result.status));
    return result.config;
}

void SaveWorldSpaceYaw(bool world_space_yaw) {
    Save("[General] WorldSpaceYaw", [world_space_yaw](Config& c) { c.world_space_yaw = world_space_yaw; });
}

void SaveTrackingMode(cameraunlock::TrackingMode mode) {
    const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(mode);
    Save("[General] RotationEnabled and [Position] PositionEnabled", [channels](Config& c) {
        c.rotation_enabled = channels.rotation_enabled;
        c.position_enabled = channels.position_enabled;
    });
}

}  // namespace tow_ht::config

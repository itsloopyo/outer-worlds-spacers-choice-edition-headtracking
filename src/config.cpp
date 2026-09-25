// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include <cstdio>
#include <windows.h>

#include "legacy_config/legacy_config.h"
#include "logging.h"

namespace tow_ht {

namespace {

constexpr const char* kIniName = "HeadTracking.ini";

std::string ini_path(const std::string& exe_dir) {
    return exe_dir + "\\" + kIniName;
}

}  // namespace

void config_load(const std::string& exe_dir, Config& out) {
    legacy::Config read;
    legacy::Load(exe_dir, read);

    out.udp_port = read.udp_port;
    out.enable_on_startup = read.enable_on_startup;
    out.world_space_yaw = read.world_space_yaw;
    out.center_window = read.center_window;
    out.yaw_sensitivity = read.yaw_sensitivity;
    out.pitch_sensitivity = read.pitch_sensitivity;
    out.roll_sensitivity = read.roll_sensitivity;
    out.invert_yaw = read.invert_yaw;
    out.invert_pitch = read.invert_pitch;
    out.invert_roll = read.invert_roll;
    out.local_smoothing = read.local_smoothing;
    out.remote_smoothing = read.remote_smoothing;
    out.position_enabled = read.position_enabled;
    out.position_sensitivity_x = read.position_sensitivity_x;
    out.position_sensitivity_y = read.position_sensitivity_y;
    out.position_sensitivity_z = read.position_sensitivity_z;
    out.limit_x = read.limit_x;
    out.limit_y = read.limit_y;
    out.limit_z = read.limit_z;
    out.limit_z_back = read.limit_z_back;
    out.reticle_enabled = read.reticle_enabled;
    out.reticle_targets = read.reticle_targets;
    out.aim_trace_channel = read.aim_trace_channel;
    out.aim_trace_distance = read.aim_trace_distance;
    out.collision_enabled = read.collision_enabled;
    out.collision_radius = read.collision_radius;
    out.collision_channel = read.collision_channel;
    out.collision_release_smoothing = read.collision_release_smoothing;
    out.widget_dump = read.widget_dump;
    out.widget_dump_outer = read.widget_dump_outer;
    out.pose_log = read.pose_log;
    out.inject_mode = read.inject_mode;
    out.yaw_mode_key = read.yaw_mode_key;
}

void config_write_default_if_missing(const std::string& exe_dir) {
    const std::string p = ini_path(exe_dir);
    if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES) return;

    FILE* f = nullptr;
    const errno_t err = fopen_s(&f, p.c_str(), "w");
    if (!f) {
        Log::Line("config: could not create %s (error %d) - the mod runs on its "
                  "compiled defaults and there is no file to edit",
                  p.c_str(), static_cast<int>(err));
        return;
    }
    std::fprintf(f,
        "; Outer Worlds: Spacer's Choice Edition Head Tracking - configuration\n"
        "; Edit values, restart the game to apply.\n\n"
        "[Network]\n"
        "UdpPort=4242\n\n"
        "[General]\n"
        "EnableOnStartup=1\n"
        "; Yaw mode the mod starts in. 1 = horizon-locked: head yaw turns the\n"
        "; view about the world up-axis, so the horizon stays level however far\n"
        "; the mouse has pitched the camera. 0 = camera-local, which leans the\n"
        "; horizon on a pitched turn. Page Down switches it for the session.\n"
        "WorldSpaceYaw=1\n"
        "; Centre the game window on your monitor's work area at startup. Only\n"
        "; ever moves a windowed game - fullscreen and borderless are left alone,\n"
        "; and so is a window already centred there. Set to 0 to keep the window\n"
        "; wherever the game or you put it.\n"
        "CenterWindow=1\n\n"
        "[Hotkeys]\n"
        "; Virtual-key code for the yaw-mode toggle. 0x22 is Page Down. It\n"
        "; cannot be a key this mod already uses - End (0x23) or Page Up (0x21)\n"
        "; - because one press would then fire both actions.\n"
        "; Set it to one of those and Page Down is kept, with a line saying so\n"
        "; in HeadTracking.log.\n"
        "YawModeKey=0x22\n\n"
        "[Rotation]\n"
        "YawSensitivity=1.0\n"
        "PitchSensitivity=1.0\n"
        "RollSensitivity=1.0\n"
        "InvertYaw=0\n"
        "InvertPitch=0\n"
        "InvertRoll=0\n"
        "; Smoothing is picked per connection from the packet source address and\n"
        "; covers rotation and position alike. 0.0 = none, 1.0 = heavy.\n"
        "; LocalSmoothing:  tracker runs on this machine (loopback).\n"
        "; RemoteSmoothing: tracker is a remote device on the network.\n"
        "LocalSmoothing=0.0\n"
        "RemoteSmoothing=0.15\n\n"
        "[Position]\n"
        "Enabled=1\n"
        "SensitivityX=1.0\n"
        "SensitivityY=1.0\n"
        "SensitivityZ=1.0\n"
        "; Lean limits in meters. Z is asymmetric: more room to lean forward\n"
        "; than back, so the view does not end up inside your own character.\n"
        "LimitX=0.30\n"
        "LimitY=0.20\n"
        "LimitZ=0.40\n"
        "LimitZBack=0.10\n\n"
        "[Reticle]\n"
        "; The game draws its crosshair at the middle of the picture, which stops\n"
        "; being where the round goes once the head moves the view. These are the\n"
        "; UMG widgets moved to meet the shot. Names come from a running game -\n"
        "; set [Dev] WidgetDump=1 and read HeadTracking.log. Name or Name@Outer,\n"
        "; comma separated, where Outer is text found in the widget's chain of\n"
        "; parent objects joined with / (for example Reticle/WidgetTree).\n"
        "Enabled=1\n"
        "Targets=%s\n\n"
        "[Aim]\n"
        "; The cast that gives the crosshair the live distance to what you are\n"
        "; pointing at. Without it the mark is right at one range only.\n"
        "; MaxDistance is in centimeters; past it the crosshair marks the aim\n"
        "; direction instead of a point.\n"
        "TraceChannel=0\n"
        "MaxDistance=20000\n\n"
        "[Collision]\n"
        "; Stops a lean putting the view inside a wall. Off until the sweep has\n"
        "; been confirmed engaging on real geometry in this game - the log says\n"
        "; so on every contact. Radius is how far off a surface the eye is held,\n"
        "; in centimeters. ReleaseSmoothing is how quickly the lean reopens once\n"
        "; an obstruction clears; tightening is always instant.\n"
        "Enabled=0\n"
        "Radius=12.0\n"
        "Channel=0\n"
        "ReleaseSmoothing=0.9\n\n"
        "[Dev]\n"
        "WidgetDump=0\n",
        legacy::kDefaultReticleTargets);
    std::fclose(f);
}

}  // namespace tow_ht

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// What the frozen pre-canonical reader makes of a HeadTracking.ini.
//
// The legacy import converts an older file through this reader, so these lock
// the reading an old file gets: that every key reaches the field it names, that
// a key the file leaves out keeps its compiled default, and how a bad value is
// handled. config_sanitize_tests covers the value guards on their own, and
// tests/config_differential/ holds the reader to the published build over the
// whole mutation corpus.
//
// Runs against a temporary directory, so it needs no game and no install.

#include "legacy_config/legacy_config.h"
#include "test_harness.h"

#include <cstdio>
#include <string>

#include <windows.h>

namespace {

namespace ht = tow_ht::legacy;

// A directory of our own under %TEMP%, so a run cannot read or write a real
// install's ini. Created on first use and idempotent after that.
std::string ScratchDir() {
    char temp[MAX_PATH] = "";
    GetTempPathA(MAX_PATH, temp);
    char unique[MAX_PATH];
    std::snprintf(unique, sizeof(unique), "%stow_config_%lu", temp, GetCurrentProcessId());
    CreateDirectoryA(unique, nullptr);
    return unique;
}

void WriteIni(const std::string& dir, const char* body) {
    const std::string path = dir + "\\HeadTracking.ini";
    FILE* f = nullptr;
    fopen_s(&f, path.c_str(), "w");
    if (!f) return;
    std::fputs(body, f);
    std::fclose(f);
}

void RemoveIni(const std::string& dir) {
    DeleteFileA((dir + "\\HeadTracking.ini").c_str());
}

// One key per section, all set away from their compiled defaults. A section
// reader that stopped being called takes its whole block down with it, and any
// one of these catches that.
void TestEverySectionReachesItsFields() {
    const std::string dir = ScratchDir();
    WriteIni(dir,
        "[Network]\nUdpPort=5150\n"
        "[General]\nEnableOnStartup=0\nWorldSpaceYaw=0\nCenterWindow=0\n"
        "[Hotkeys]\nYawModeKey=0x24\n"
        "[Rotation]\nYawSensitivity=1.5\nPitchSensitivity=0.5\nRollSensitivity=2.0\n"
        "InvertYaw=1\nInvertPitch=1\nInvertRoll=1\n"
        "LocalSmoothing=0.25\nRemoteSmoothing=0.5\n"
        "[Position]\nEnabled=0\nSensitivityX=2.0\nSensitivityY=3.0\nSensitivityZ=4.0\n"
        "LimitX=0.11\nLimitY=0.12\nLimitZ=0.13\nLimitZBack=0.14\n"
        "[Reticle]\nEnabled=0\nTargets=Dot@HUD\n"
        "[Aim]\nTraceChannel=3\nMaxDistance=12345\n"
        "[Collision]\nEnabled=1\nRadius=20.5\nChannel=7\nReleaseSmoothing=0.75\n"
        "[Dev]\nWidgetDump=1\nWidgetDumpOuter=HUD_BP_C\nPoseLog=1\n"
        "[Diag]\nInjectMode=0\n");

    ht::Config c;
    ht::Load(dir, c);

    CHECK(c.udp_port == 5150);
    CHECK(!c.enable_on_startup);
    CHECK(!c.world_space_yaw);
    CHECK(!c.center_window);
    CHECK(c.yaw_mode_key == 0x24);

    CHECK_NEAR(c.yaw_sensitivity, 1.5, 1e-6);
    CHECK_NEAR(c.pitch_sensitivity, 0.5, 1e-6);
    CHECK_NEAR(c.roll_sensitivity, 2.0, 1e-6);
    CHECK(c.invert_yaw);
    CHECK(c.invert_pitch);
    CHECK(c.invert_roll);
    CHECK_NEAR(c.local_smoothing, 0.25, 1e-6);
    CHECK_NEAR(c.remote_smoothing, 0.5, 1e-6);

    CHECK(!c.position_enabled);
    CHECK_NEAR(c.position_sensitivity_x, 2.0, 1e-6);
    CHECK_NEAR(c.position_sensitivity_y, 3.0, 1e-6);
    CHECK_NEAR(c.position_sensitivity_z, 4.0, 1e-6);
    CHECK_NEAR(c.limit_x, 0.11, 1e-6);
    CHECK_NEAR(c.limit_y, 0.12, 1e-6);
    CHECK_NEAR(c.limit_z, 0.13, 1e-6);
    CHECK_NEAR(c.limit_z_back, 0.14, 1e-6);

    CHECK(!c.reticle_enabled);
    CHECK(c.reticle_targets == "Dot@HUD");

    CHECK(c.aim_trace_channel == 3);
    CHECK_NEAR(c.aim_trace_distance, 12345.0, 1e-3);

    CHECK(c.collision_enabled);
    CHECK_NEAR(c.collision_radius, 20.5, 1e-6);
    CHECK(c.collision_channel == 7);
    CHECK_NEAR(c.collision_release_smoothing, 0.75, 1e-6);

    CHECK(c.widget_dump);
    CHECK(c.widget_dump_outer == "HUD_BP_C");
    CHECK(c.pose_log);
    CHECK_MSG(c.inject_mode == 0, "InjectMode 0 is reachable, but only by asking for it");

    RemoveIni(dir);
}

// An ini that says nothing leaves every field on its compiled default, and the
// reticle targets on the shipped list rather than empty.
void TestAbsentKeysKeepTheCompiledDefaults() {
    const std::string dir = ScratchDir();
    WriteIni(dir, "[General]\n");

    const ht::Config fresh;
    ht::Config c;
    ht::Load(dir, c);

    CHECK(c.udp_port == fresh.udp_port);
    CHECK(c.enable_on_startup == fresh.enable_on_startup);
    CHECK(c.world_space_yaw == fresh.world_space_yaw);
    CHECK(c.center_window == fresh.center_window);
    CHECK_NEAR(c.yaw_sensitivity, fresh.yaw_sensitivity, 1e-6);
    CHECK_NEAR(c.local_smoothing, fresh.local_smoothing, 1e-6);
    CHECK_NEAR(c.remote_smoothing, fresh.remote_smoothing, 1e-6);
    CHECK(c.position_enabled == fresh.position_enabled);
    CHECK_NEAR(c.limit_z, fresh.limit_z, 1e-6);
    CHECK_NEAR(c.limit_z_back, fresh.limit_z_back, 1e-6);
    CHECK(c.reticle_enabled == fresh.reticle_enabled);
    CHECK(c.aim_trace_channel == fresh.aim_trace_channel);
    CHECK(c.collision_enabled == fresh.collision_enabled);
    CHECK(c.widget_dump == fresh.widget_dump);
    CHECK(c.pose_log == fresh.pose_log);
    CHECK_MSG(c.inject_mode == -1, "no [Diag] InjectMode means no override");
    CHECK(c.yaw_mode_key == fresh.yaw_mode_key);
    CHECK_MSG(!c.reticle_targets.empty(),
              "an absent Targets key falls back to the shipped widget list");

    RemoveIni(dir);
}

// Out of range is handled two different ways on purpose, and which key gets
// which is the thing worth locking: a float is CLAMPED to the nearest bound,
// while an int FALLS BACK to the shipped default. config_sanitize.h carries
// why - a port pinned to 65535 is as wrong as the truncation it replaces, and a
// virtual-key code clamped to 0xFE binds a key nobody has, whereas a
// sensitivity or a limit is a continuous quantity whose nearest legal value is
// the one the player was reaching for.
void TestOutOfRangeValuesAreClampedOrFallBackPerKey() {
    const std::string dir = ScratchDir();
    WriteIni(dir,
        "[Network]\nUdpPort=70000\n"
        "[Rotation]\nYawSensitivity=99\nLocalSmoothing=-1\n"
        "[Position]\nLimitX=5.0\n"
        "[Aim]\nTraceChannel=300\nMaxDistance=0\n"
        "[Collision]\nChannel=-4\n"
        "[Hotkeys]\nYawModeKey=0xFFFF\n");

    const ht::Config fresh;
    ht::Config c;
    ht::Load(dir, c);

    // Integers: back to the default.
    CHECK_MSG(c.udp_port == fresh.udp_port, "a port too wide for a uint16 is not clamped");
    CHECK_MSG(c.aim_trace_channel == fresh.aim_trace_channel,
              "a channel wider than the byte it is written into falls back");
    CHECK(c.collision_channel == fresh.collision_channel);
    CHECK_MSG(c.yaw_mode_key == fresh.yaw_mode_key,
              "a value too wide to be a virtual-key code keeps Page Down");

    // Floats: to the nearest bound the documented range allows.
    CHECK_NEAR_MSG(c.yaw_sensitivity, 3.0, 1e-6, "sensitivity clamps to its maximum");
    CHECK_NEAR_MSG(c.local_smoothing, 0.0, 1e-6, "smoothing clamps to its minimum");
    CHECK_NEAR_MSG(c.limit_x, 0.5, 1e-6, "a lean limit clamps to its maximum");
    CHECK_NEAR_MSG(c.aim_trace_distance, 1.0, 1e-3,
                   "the cast distance clamps to its one-centimetre minimum");

    RemoveIni(dir);
}

// A NaN is a finite-range test's blind spot - both comparisons are false - so
// it is the case the float guard exists for, and it falls back rather than
// travelling into the camera.
void TestNonFiniteValuesFallBack() {
    const std::string dir = ScratchDir();
    WriteIni(dir,
        "[Rotation]\nYawSensitivity=nan\nRollSensitivity=inf\n"
        "[Position]\nLimitZ=nan\n");

    const ht::Config fresh;
    ht::Config c;
    ht::Load(dir, c);

    CHECK_NEAR(c.yaw_sensitivity, fresh.yaw_sensitivity, 1e-6);
    CHECK_NEAR_MSG(c.roll_sensitivity, fresh.roll_sensitivity, 1e-6,
                   "an infinity is not a large value - it is replaced by the fallback, "
                   "not clamped to the maximum");
    CHECK_NEAR(c.limit_z, fresh.limit_z, 1e-6);

    RemoveIni(dir);
}

// A trailing comment is attached to the value on Windows, so every key that
// takes one has to stop at it.
void TestTrailingCommentsDoNotChangeAValue() {
    const std::string dir = ScratchDir();
    WriteIni(dir,
        "[Network]\nUdpPort=5151 ; the tracker port\n"
        "[General]\nEnableOnStartup=1 ; on\n");

    ht::Config c;
    ht::Load(dir, c);

    CHECK(c.udp_port == 5151);
    CHECK(c.enable_on_startup);

    RemoveIni(dir);
}

// YawModeKey is the one rebindable key, and the poller dispatches to every
// handler registered on a code - so a YawModeKey that names a key the mod
// already binds fires two actions on one press. The range check cannot see it
// (0x23 is a perfectly good virtual-key code), so it is its own test.
void TestYawModeKeyCannotTakeAKeyTheModAlreadyBinds() {
    const std::string dir = ScratchDir();
    const ht::Config fresh;

    for (const int taken : {0x23 /*End*/, 0x21 /*PageUp*/}) {
        char body[64];
        std::snprintf(body, sizeof(body), "[Hotkeys]\nYawModeKey=0x%X\n", taken);
        WriteIni(dir, body);

        ht::Config c;
        ht::Load(dir, c);
        CHECK_MSG(c.yaw_mode_key == fresh.yaw_mode_key,
                  "a YawModeKey already owned by another binding keeps Page Down");
    }

    // A code the mod does not bind is still the player's to choose, including
    // the two the fleet leaves deliberately free.
    WriteIni(dir, "[Hotkeys]\nYawModeKey=0x24\n");
    ht::Config home;
    ht::Load(dir, home);
    CHECK_MSG(home.yaw_mode_key == 0x24,
              "an unclaimed virtual-key code is accepted unchanged");

    RemoveIni(dir);
}

// A value that is present but not a number must fall back to the compiled
// default AND say so, rather than being accepted as whatever its leading
// characters came to.
//
// IniReader::ReadFloat is a prefix parse, and the range check downstream only
// speaks when it changes the value - so a key whose prefix lands inside its own
// range used to be accepted in total silence. `RemoteSmoothing=0,15` is the case
// that matters: a decimal comma is what a German or French keyboard writes, the
// prefix is 0, and 0 is a legal smoothing value.
void TestPartiallyParsedFloatsFallBack() {
    const std::string dir = ScratchDir();
    // The three comma values are chosen so the prefix parse lands somewhere
    // OTHER than the default; one whose prefix happened to equal the default
    // would pass whether or not the parse was fixed. `banana` is the separate
    // no-parse-at-all case, which the old code already handled - it is here to
    // lock that behaviour, not to discriminate.
    WriteIni(dir,
        "[Rotation]\nRemoteSmoothing=0,15\nYawSensitivity=banana\n"
        "[Position]\nSensitivityX=2,5\n"
        "[Collision]\nReleaseSmoothing=0,5\n");

    const ht::Config fresh;
    ht::Config c;
    ht::Load(dir, c);

    CHECK_NEAR_MSG(c.remote_smoothing, fresh.remote_smoothing, 1e-6,
                   "a decimal comma is not a number, so the default stands rather "
                   "than the 0.0 its leading character parses to");
    CHECK_NEAR_MSG(c.collision_release_smoothing, fresh.collision_release_smoothing,
                   1e-6, "0,5 keeps the 0.9 default rather than the 0.0 it prefixes to");
    CHECK_NEAR_MSG(c.yaw_sensitivity, fresh.yaw_sensitivity, 1e-6,
                   "a value that does not parse at all keeps the compiled default");
    CHECK_NEAR(c.position_sensitivity_x, fresh.position_sensitivity_x, 1e-6);

    RemoveIni(dir);
}

// A hex-spelled integer is not a decimal one. The shipped ini teaches the 0x
// spelling on YawModeKey, so it is a spelling players copy onto other keys, and
// a base-10 parse reads 0x1 as 1's worth of nothing: it stops at the x and
// reports success with the value 0. For [Diag] InjectMode that 0 is the mode
// that hands every caller the head pose, which is the aim decoupling off.
void TestHexSpelledIntegerKeysDoNotReadAsZero() {
    const std::string dir = ScratchDir();
    // [Aim] TraceChannel is deliberately NOT tested here: it defaults to 0 and a
    // base-10 prefix parse of 0x02 also yields 0, so the assertion would pass
    // whether or not the parse was fixed. [Collision] Radius stands in - it is a
    // float key, its default is 12.0, and _strtod_l reads 0x12 as 18.0.
    WriteIni(dir, "[Diag]\nInjectMode=0x1\n[Collision]\nRadius=+0x12\n");

    const ht::Config fresh;
    ht::Config c;
    ht::Load(dir, c);

    CHECK_MSG(c.inject_mode == fresh.inject_mode,
              "a hex-spelled InjectMode is rejected, not read as the all-callers mode");
    CHECK_NEAR_MSG(c.collision_radius, fresh.collision_radius, 1e-6,
                   "a hex-spelled Radius is rejected, not read as 18.0cm");

    RemoveIni(dir);
}

// An ini written by an older release still carries the retired AdsMode key. It
// is ignored, and the keys around it in the same section still load.
void TestRetiredAdsModeKeyIsIgnored() {
    const std::string dir = ScratchDir();
    WriteIni(dir, "[Aim]\nAdsMode=marker\nTraceChannel=3\nMaxDistance=12345\n");
    ht::Config c;
    ht::Load(dir, c);
    CHECK(c.aim_trace_channel == 3);
    CHECK_NEAR(c.aim_trace_distance, 12345.0, 1e-3);
    RemoveIni(dir);
}

}  // namespace

int main() {
    TestRetiredAdsModeKeyIsIgnored();
    TestEverySectionReachesItsFields();
    TestAbsentKeysKeepTheCompiledDefaults();
    TestOutOfRangeValuesAreClampedOrFallBackPerKey();
    TestNonFiniteValuesFallBack();
    TestPartiallyParsedFloatsFallBack();
    TestHexSpelledIntegerKeysDoNotReadAsZero();
    TestTrailingCommentsDoNotChangeAValue();
    TestYawModeKeyCannotTakeAKeyTheModAlreadyBinds();
    RemoveDirectoryA(ScratchDir().c_str());
    return tow_test::Report();
}

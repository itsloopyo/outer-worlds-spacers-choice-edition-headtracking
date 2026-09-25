// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// HeadTracking.ini in the canonical config format.
//
// The committed config/HeadTracking.ini is the file the table renders from its
// defaults, which is also what the owner creates at first launch. A toggle's
// save changes the lines of its rows and no other byte. An older file is
// converted through the frozen import; tests/config_differential/ holds that to
// the published build over the whole corpus, and the cases here are the ones
// worth reading as examples.
//
// `tow_config_tests --render-config <path>` writes the rendered defaults to
// <path> and exits, which is how `pixi run render-config` rewrites the committed
// file after a change to a row, a comment or a default.

#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include <windows.h>

#include "config.h"
#include "test_harness.h"

namespace {

namespace cfg = ::cameraunlock::config;
using cameraunlock::TrackingMode;

std::string ReadFileBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteFileBytes(const std::string& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("cannot write " + path);
}

std::string Rendered() {
    const cfg::ConfigTable<tow_ht::Config> table = tow_ht::config::Table();
    return cfg::RenderCanonical(table, table.defaults(), tow_ht::config::Header());
}

std::string CommittedFile() { return ReadFileBytes(std::string(TOW_SOURCE_DIR) + "/config/HeadTracking.ini"); }

// A folder of its own per case, emptied and removed afterwards.
class Scratch {
public:
    explicit Scratch(const char* tag) {
        char temp[MAX_PATH] = {};
        GetTempPathA(MAX_PATH, temp);
        dir_ = std::string(temp) + "tow_ht_config_" + tag + "_" + std::to_string(GetCurrentProcessId());
        if (!CreateDirectoryA(dir_.c_str(), nullptr)) throw std::runtime_error("cannot create " + dir_);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    ~Scratch() {
        WIN32_FIND_DATAA found;
        const HANDLE h = FindFirstFileA((dir_ + "\\*").c_str(), &found);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileA((dir_ + "\\" + found.cFileName).c_str());
            } while (FindNextFileA(h, &found));
            FindClose(h);
        }
        RemoveDirectoryA(dir_.c_str());
    }

    std::wstring wdir() const { return std::wstring(dir_.begin(), dir_.end()); }
    std::string ini() const { return dir_ + "\\HeadTracking.ini"; }

private:
    std::string dir_;
};

std::vector<std::string> Lines(const std::string& bytes) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (std::size_t end; (end = bytes.find("\r\n", start)) != std::string::npos; start = end + 2) {
        lines.push_back(bytes.substr(start, end - start));
    }
    return lines;
}

// The lines that differ between two files of the same line count, or "count" when
// the counts differ.
std::vector<std::string> ChangedLines(const std::string& before, const std::string& after) {
    const std::vector<std::string> a = Lines(before), b = Lines(after);
    if (a.size() != b.size()) return {"count"};
    std::vector<std::string> changed;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) changed.push_back(b[i]);
    }
    return changed;
}

bool Holds(const std::string& bytes, const char* line) {
    return bytes.find(std::string("\r\n") + line + "\r\n") != std::string::npos;
}

void TheCommittedFileIsTheRenderedDefaults() {
    CHECK_MSG(Rendered() == CommittedFile(),
              "config/HeadTracking.ini is the table's render of its defaults; run pixi run render-config");
}

void FirstLaunchCreatesTheCommittedFile() {
    Scratch s("created");
    const tow_ht::Config loaded = tow_ht::config::Load(s.wdir());
    CHECK_MSG(ReadFileBytes(s.ini()) == CommittedFile(), "the first launch writes the committed file byte for byte");
    CHECK(loaded.toggle_key == "End, Ctrl+Shift+Y");
    CHECK(loaded.cycle_tracking_mode_key == "PageUp, Ctrl+Shift+G");
    CHECK(loaded.yaw_mode_key == "PageDown, Ctrl+Shift+H");
    CHECK(loaded.inject_mode_key == "Ctrl+Shift+J");
}

void TheYawToggleSavesItsLineAndNothingElse() {
    Scratch s("save_yaw");
    tow_ht::config::Load(s.wdir());
    const std::string before = ReadFileBytes(s.ini());
    tow_ht::config::SaveWorldSpaceYaw(false);
    const std::vector<std::string> changed = ChangedLines(before, ReadFileBytes(s.ini()));
    CHECK_MSG(changed == std::vector<std::string>{"WorldSpaceYaw=false"}, "a yaw save changes WorldSpaceYaw alone");
    CHECK_MSG(!tow_ht::config::Load(s.wdir()).world_space_yaw, "the saved yaw mode comes back at the next launch");
}

void TheModeCycleSavesThePair() {
    Scratch s("save_mode");
    tow_ht::config::Load(s.wdir());
    const std::string before = ReadFileBytes(s.ini());

    tow_ht::config::SaveTrackingMode(TrackingMode::RotationOnly);
    CHECK_MSG(ChangedLines(before, ReadFileBytes(s.ini())) == std::vector<std::string>{"PositionEnabled=false"},
              "rotation only changes PositionEnabled alone");

    tow_ht::config::SaveTrackingMode(TrackingMode::PositionOnly);
    CHECK_MSG(ChangedLines(before, ReadFileBytes(s.ini())) == std::vector<std::string>{"RotationEnabled=false"},
              "position only writes the pair: RotationEnabled false, PositionEnabled back to true");
    const tow_ht::Config reloaded = tow_ht::config::Load(s.wdir());
    CHECK_MSG(!reloaded.rotation_enabled && reloaded.position_enabled, "the saved mode comes back at the next launch");

    tow_ht::config::SaveTrackingMode(TrackingMode::RotationAndPosition);
    CHECK_MSG(ReadFileBytes(s.ini()) == before, "back to full, the file is as it was created");
}

// The old reader applied its one vertical limit both ways. The conversion
// writes both bounds.
void AnOldLimitYReachesBothBounds() {
    Scratch s("limit_y");
    WriteFileBytes(s.ini(), "[Position]\r\nLimitY=0.40\r\n");
    const tow_ht::Config c = tow_ht::config::Load(s.wdir());
    CHECK(c.limit_y == 0.4f);
    CHECK(c.limit_y_down == 0.4f);
    const std::string migrated = ReadFileBytes(s.ini());
    CHECK(Holds(migrated, "PositionLimitY=0.4"));
    CHECK(Holds(migrated, "PositionLimitYDown=0.4"));
    CHECK_MSG(ReadFileBytes(s.ini() + ".pre-canonical") == "[Position]\r\nLimitY=0.40\r\n",
              "the old file is kept as HeadTracking.ini.pre-canonical");
}

// The yaw key was the one hotkey in the old file. It joins its chord in the list;
// End, Page Up and the inject chord, bound in code before, are written out.
void AnOldYawKeyJoinsItsChord() {
    Scratch s("yaw_key");
    WriteFileBytes(s.ini(), "[Hotkeys]\r\nYawModeKey=0x2E\r\n[Aim]\r\nAdsMode=tracked\r\n");
    const tow_ht::Config c = tow_ht::config::Load(s.wdir());
    CHECK(c.yaw_mode_key == "Delete, Ctrl+Shift+H");
    CHECK(c.toggle_key == "End, Ctrl+Shift+Y");
    CHECK(c.cycle_tracking_mode_key == "PageUp, Ctrl+Shift+G");
    CHECK(c.inject_mode_key == "Ctrl+Shift+J");
    CHECK_MSG(ReadFileBytes(s.ini()).find("AdsMode") == std::string::npos, "the retired ADS key is not carried");
}

// [Position] Enabled=0 started the mod in rotation only, and the cycle still
// reached the position modes, so it is the startup mode and not a feature switch.
void AnOldPositionSwitchIsTheStartupMode() {
    Scratch s("position");
    WriteFileBytes(s.ini(), "[Position]\r\nEnabled=0\r\n");
    const tow_ht::Config c = tow_ht::config::Load(s.wdir());
    CHECK(c.rotation_enabled);
    CHECK(!c.position_enabled);
}

// Sensitivities and inversions shipped as identity, so nothing is folded, and one
// a player changed is not carried. The smoothing pair moves to [Smoothing].
void AChangedSensitivityIsNotCarried() {
    Scratch s("sensitivity");
    WriteFileBytes(s.ini(), "[Rotation]\r\nYawSensitivity=2.50\r\nInvertPitch=1\r\nRemoteSmoothing=0.40\r\n"
                            "[Position]\r\nSensitivityX=2.0\r\n");
    const tow_ht::Config c = tow_ht::config::Load(s.wdir());
    CHECK(c.remote_smoothing == 0.4f);
    const std::string migrated = ReadFileBytes(s.ini());
    CHECK(Holds(migrated, "[Smoothing]"));
    CHECK(Holds(migrated, "RemoteSmoothing=0.4"));
    CHECK(migrated.find("Sensitivity") == std::string::npos);
    CHECK(migrated.find("Invert") == std::string::npos);
    CHECK(migrated.find("[Rotation]") == std::string::npos);
}

// The crosshair always follows the aim now, over the widgets the mod names, so the
// old switch and widget list are not carried.
void TheOldReticleSettingsAreNotCarried() {
    Scratch s("reticle");
    WriteFileBytes(s.ini(), "[Reticle]\r\nEnabled=0\r\nTargets=Dot@HUD\r\n");
    tow_ht::config::Load(s.wdir());
    const std::string migrated = ReadFileBytes(s.ini());
    CHECK(migrated.find("[Reticle]") == std::string::npos);
    CHECK(migrated.find("Targets") == std::string::npos);
}

// The lean sweep shipped switched off pending verification, so its switch
// follows the mod's default; the rest of [Collision] is carried.
void AnOldCollisionSwitchFollowsTheDefault() {
    Scratch s("collision");
    WriteFileBytes(s.ini(), "[Collision]\r\nEnabled=1\r\nRadius=20.5\r\nChannel=3\r\nReleaseSmoothing=0.5\r\n");
    const tow_ht::Config c = tow_ht::config::Load(s.wdir());
    CHECK(!c.collision_enabled);
    CHECK(c.collision_margin == 20.5f);
    CHECK(c.collision_channel == 3);
    CHECK(c.collision_release_smoothing == 0.5f);
    const std::string migrated = ReadFileBytes(s.ini());
    CHECK(Holds(migrated, "CollisionEnabled=false"));
    CHECK(Holds(migrated, "CollisionMargin=20.5"));
    CHECK(Holds(migrated, "CollisionChannel=3"));
}

// The keys that only this game has keep their values under their new names.
void TheGameKeysAreCarried() {
    Scratch s("local");
    WriteFileBytes(s.ini(), "[General]\r\nCenterWindow=0\r\n[Aim]\r\nTraceChannel=2\r\nMaxDistance=5000\r\n"
                            "[Dev]\r\nWidgetDump=1\r\nWidgetDumpOuter=HUD_BP_C\r\nPoseLog=1\r\n"
                            "[Diag]\r\nInjectMode=0\r\n");
    const tow_ht::Config c = tow_ht::config::Load(s.wdir());
    CHECK(!c.center_window);
    CHECK(c.aim_trace_channel == 2);
    CHECK(c.aim_trace_distance == 5000.0f);
    CHECK(c.widget_dump);
    CHECK(c.widget_dump_outer == "HUD_BP_C");
    CHECK(c.pose_log);
    CHECK(c.inject_mode == 0);
    const std::string migrated = ReadFileBytes(s.ini());
    CHECK(Holds(migrated, "CenterWindow=false"));
    CHECK(Holds(migrated, "AimTraceChannel=2"));
    CHECK(Holds(migrated, "InjectMode=0"));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--render-config") == 0) {
        WriteFileBytes(argv[2], Rendered());
        return 0;
    }

    TheCommittedFileIsTheRenderedDefaults();
    FirstLaunchCreatesTheCommittedFile();
    TheYawToggleSavesItsLineAndNothingElse();
    TheModeCycleSavesThePair();
    AnOldLimitYReachesBothBounds();
    AnOldYawKeyJoinsItsChord();
    AnOldPositionSwitchIsTheStartupMode();
    AChangedSensitivityIsNotCarried();
    TheOldReticleSettingsAreNotCarried();
    AnOldCollisionSwitchFollowsTheDefault();
    TheGameKeysAreCarried();

    return tow_test::Report();
}

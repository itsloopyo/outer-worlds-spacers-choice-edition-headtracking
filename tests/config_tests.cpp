// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// CameraUnlock.ini in the canonical config format.
//
// The committed config/HeadTracking.ini is the table's fresh render, which is
// also what the owner creates as CameraUnlock.ini at first launch: default on
// every global row, so those rows follow Defaults.ini. A toggle's save changes
// the lines of its rows and no other byte. HeadTracking.ini, the file older
// builds read, is imported once while CameraUnlock.ini is absent and never
// written; tests/config_differential/ holds that import to the published build
// over the whole corpus, and the cases here are the ones worth reading as
// examples.
//
// `tow_config_tests --render-config <path>` writes the fresh render to <path>
// and exits, which is how `pixi run render-config` rewrites the committed file
// after a change to a row, a comment or a default.

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

bool Exists(const std::string& path) { return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::wstring Wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }

std::string Rendered() { return cfg::RenderCanonicalFresh(tow_ht::config::Table(), tow_ht::config::Header()); }

std::string CommittedFile() { return ReadFileBytes(std::string(TOW_SOURCE_DIR) + "/config/HeadTracking.ini"); }

void DeleteFolder(const std::string& dir) {
    WIN32_FIND_DATAA found;
    const HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &found);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileA((dir + "\\" + found.cFileName).c_str());
        } while (FindNextFileA(h, &found));
        FindClose(h);
    }
    RemoveDirectoryA(dir.c_str());
}

// A game folder of its own per case, and a folder beside it for Defaults.ini,
// emptied and removed afterwards.
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
        DeleteFolder(dir_);
        DeleteFolder(global());
    }

    std::wstring wdir() const { return Wide(dir_); }
    std::string ini() const { return dir_ + "\\CameraUnlock.ini"; }
    std::string legacy() const { return dir_ + "\\HeadTracking.ini"; }
    std::string global() const { return dir_ + "_global"; }
    std::string defaults_ini() const { return global() + "\\Defaults.ini"; }
    cfg::DefaultsFile defaults() const { return cfg::DefaultsFile::At(Wide(defaults_ini())); }

    // Defaults.ini as a player edited it, before the first launch.
    void WriteDefaults(const std::string& bytes) const {
        if (!CreateDirectoryA(global().c_str(), nullptr)) throw std::runtime_error("cannot create " + global());
        WriteFileBytes(defaults_ini(), bytes);
    }

    tow_ht::Config Load() const { return tow_ht::config::Load(wdir(), defaults()); }

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

void TheCommittedFileIsTheFreshRender() {
    CHECK_MSG(Rendered() == CommittedFile(),
              "config/HeadTracking.ini is the table's fresh render; run pixi run render-config");
}

// Every global row holds default, and the game's own rows hold their values:
// the lean margin in centimetres, and the two channels as comments.
void TheCommittedFileHoldsDefaultOnEveryGlobalRow() {
    const std::string committed = CommittedFile();
    for (const char* line : {"UdpPort=default", "EnableOnStartup=default", "WorldSpaceYaw=default",
                             "RotationEnabled=default", "LocalSmoothing=default", "RemoteSmoothing=default",
                             "PositionEnabled=default", "PositionLimitX=default", "PositionLimitY=default",
                             "PositionLimitYDown=default", "PositionLimitZ=default", "PositionLimitZBack=default",
                             "CollisionEnabled=default", "CollisionReleaseSmoothing=default", "ToggleKey=default",
                             "CycleTrackingModeKey=default", "YawModeKey=default", "; CollisionChannel=0",
                             "; AimTraceChannel=0", "CenterWindow=true"}) {
        CHECK_MSG(Holds(committed, line), line);
    }
    // Active or commented at its default: whether core renders a row that is not
    // global as an Engine row is core's to say.
    CHECK_MSG(committed.find("CollisionMargin=12.0\r\n") != std::string::npos,
              "the lean margin is the game's own 12 cm, never default");
}

void FirstLaunchCreatesTheCommittedFile() {
    Scratch s("created");
    const tow_ht::Config loaded = s.Load();
    CHECK_MSG(ReadFileBytes(s.ini()) == CommittedFile(), "the first launch writes the committed file byte for byte");
    CHECK_MSG(!Exists(s.legacy()), "no HeadTracking.ini is written");
    CHECK_MSG(Exists(s.defaults_ini()), "the first launch creates Defaults.ini where there is none");
    CHECK(loaded.udp_port == 4242);
    CHECK(loaded.world_space_yaw);
    CHECK(loaded.collision_enabled);
    CHECK(loaded.collision_margin == 12.0f);
    CHECK(loaded.toggle_key == "End, Ctrl+Shift+Y");
    CHECK(loaded.cycle_tracking_mode_key == "PageUp, Ctrl+Shift+G");
    CHECK(loaded.yaw_mode_key == "PageDown, Ctrl+Shift+H");
    CHECK(loaded.inject_mode_key == "Ctrl+Shift+J");
}

// A row holding default takes Defaults.ini's value; the game's own rows never do.
void DefaultRowsFollowDefaultsIni() {
    Scratch s("follows");
    s.WriteDefaults("[General]\r\nWorldSpaceYaw=false\r\n[Hotkeys]\r\nToggleKey=F8\r\n"
                    "[Position]\r\nCollisionMargin=99.0\r\n");
    const tow_ht::Config loaded = s.Load();
    CHECK_MSG(ReadFileBytes(s.ini()) == CommittedFile(), "a new CameraUnlock.ini is the committed file whatever Defaults.ini says");
    CHECK(!loaded.world_space_yaw);
    CHECK(loaded.toggle_key == "F8");
    CHECK_MSG(loaded.collision_margin == 12.0f, "CollisionMargin is the game's own, never Defaults.ini's");
}

void TheYawToggleSavesItsLineAndNothingElse() {
    Scratch s("save_yaw");
    s.Load();
    const std::string before = ReadFileBytes(s.ini());
    const std::string defaults_before = ReadFileBytes(s.defaults_ini());
    tow_ht::config::SaveWorldSpaceYaw(false);
    const std::vector<std::string> changed = ChangedLines(before, ReadFileBytes(s.ini()));
    CHECK_MSG(changed == std::vector<std::string>{"WorldSpaceYaw=false"}, "a yaw save changes WorldSpaceYaw alone");
    CHECK_MSG(ReadFileBytes(s.defaults_ini()) == defaults_before, "a save leaves Defaults.ini as it was");
    CHECK_MSG(!s.Load().world_space_yaw, "the saved yaw mode comes back at the next launch");

    // Once saved, the row holds a value, so Defaults.ini no longer reaches it.
    tow_ht::config::SaveWorldSpaceYaw(true);
    CHECK_MSG(ChangedLines(before, ReadFileBytes(s.ini())) == std::vector<std::string>{"WorldSpaceYaw=true"},
              "saving the default value writes it, not default");
}

void TheModeCycleSavesThePair() {
    Scratch s("save_mode");
    s.Load();
    const std::string before = ReadFileBytes(s.ini());

    tow_ht::config::SaveTrackingMode(TrackingMode::RotationOnly);
    CHECK_MSG(ChangedLines(before, ReadFileBytes(s.ini())) ==
                  (std::vector<std::string>{"RotationEnabled=true", "PositionEnabled=false"}),
              "a mode save writes both rows of the pair over default");

    tow_ht::config::SaveTrackingMode(TrackingMode::PositionOnly);
    CHECK_MSG(ChangedLines(before, ReadFileBytes(s.ini())) ==
                  (std::vector<std::string>{"RotationEnabled=false", "PositionEnabled=true"}),
              "position only writes the pair");
    const tow_ht::Config reloaded = s.Load();
    CHECK_MSG(!reloaded.rotation_enabled && reloaded.position_enabled, "the saved mode comes back at the next launch");
}

// HeadTracking.ini is imported into a new CameraUnlock.ini and left byte for byte
// as it was, with no copy of it beside it.
void AnOldFileIsImportedAndLeftAsItWas() {
    Scratch s("import");
    const std::string old = "[General]\r\nWorldSpaceYaw=0\r\n[Position]\r\nLimitY=0.40\r\n";
    WriteFileBytes(s.legacy(), old);
    const tow_ht::Config c = s.Load();
    CHECK(!c.world_space_yaw);
    CHECK(c.limit_y == 0.4f);
    CHECK_MSG(c.limit_y_down == 0.4f, "the old reader applied its one vertical limit both ways");
    CHECK_MSG(ReadFileBytes(s.legacy()) == old, "HeadTracking.ini keeps its bytes");
    CHECK_MSG(!Exists(s.legacy() + ".pre-canonical"), "no copy of HeadTracking.ini is made");
    const std::string migrated = ReadFileBytes(s.ini());
    CHECK(Holds(migrated, "WorldSpaceYaw=false"));
    CHECK(Holds(migrated, "PositionLimitY=0.4"));
    CHECK(Holds(migrated, "PositionLimitYDown=0.4"));
    CHECK_MSG(Holds(migrated, "UdpPort=default"), "a value equal to what default gives is written as default");

    // CameraUnlock.ini now exists, so HeadTracking.ini is not read again.
    WriteFileBytes(s.legacy(), "[General]\r\nWorldSpaceYaw=1\r\n");
    CHECK_MSG(!s.Load().world_space_yaw, "the next start reads CameraUnlock.ini, not HeadTracking.ini");
    CHECK_MSG(ReadFileBytes(s.ini()) == migrated, "the next start rewrites nothing");
}

// Where Defaults.ini already holds the player's value, the import writes default.
void AnImportedValueEqualToDefaultsIniIsWrittenAsDefault() {
    Scratch s("import_default");
    s.WriteDefaults("[General]\r\nWorldSpaceYaw=false\r\n");
    WriteFileBytes(s.legacy(), "[General]\r\nWorldSpaceYaw=0\r\n");
    CHECK(!s.Load().world_space_yaw);
    CHECK(Holds(ReadFileBytes(s.ini()), "WorldSpaceYaw=default"));
}

// The yaw key was the one hotkey in the old file. It joins its chord in the list;
// End, Page Up and the inject chord, bound in code before, are written out.
void AnOldYawKeyJoinsItsChord() {
    Scratch s("yaw_key");
    WriteFileBytes(s.legacy(), "[Hotkeys]\r\nYawModeKey=0x2E\r\n[Aim]\r\nAdsMode=tracked\r\n");
    const tow_ht::Config c = s.Load();
    CHECK(c.yaw_mode_key == "Delete, Ctrl+Shift+H");
    CHECK(c.toggle_key == "End, Ctrl+Shift+Y");
    CHECK(c.cycle_tracking_mode_key == "PageUp, Ctrl+Shift+G");
    CHECK(c.inject_mode_key == "Ctrl+Shift+J");
    const std::string migrated = ReadFileBytes(s.ini());
    CHECK(Holds(migrated, "YawModeKey=Delete, Ctrl+Shift+H"));
    CHECK(Holds(migrated, "ToggleKey=default"));
    CHECK_MSG(migrated.find("AdsMode") == std::string::npos, "the retired ADS key is not carried");
}

// [Position] Enabled=0 started the mod in rotation only, and the cycle still
// reached the position modes, so it is the startup mode and not a feature switch.
void AnOldPositionSwitchIsTheStartupMode() {
    Scratch s("position");
    WriteFileBytes(s.legacy(), "[Position]\r\nEnabled=0\r\n");
    const tow_ht::Config c = s.Load();
    CHECK(c.rotation_enabled);
    CHECK(!c.position_enabled);
    const std::string migrated = ReadFileBytes(s.ini());
    CHECK_MSG(Holds(migrated, "RotationEnabled=true") && Holds(migrated, "PositionEnabled=false"),
              "the pair is written as values when either differs from its default");
}

// Sensitivities and inversions shipped as identity, so nothing is folded, and one
// a player changed is not carried. The smoothing pair moves to [Smoothing].
void AChangedSensitivityIsNotCarried() {
    Scratch s("sensitivity");
    WriteFileBytes(s.legacy(), "[Rotation]\r\nYawSensitivity=2.50\r\nInvertPitch=1\r\nRemoteSmoothing=0.40\r\n"
                               "[Position]\r\nSensitivityX=2.0\r\n");
    const tow_ht::Config c = s.Load();
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
    WriteFileBytes(s.legacy(), "[Reticle]\r\nEnabled=0\r\nTargets=Dot@HUD\r\n");
    s.Load();
    const std::string migrated = ReadFileBytes(s.ini());
    CHECK(migrated.find("[Reticle]") == std::string::npos);
    CHECK(migrated.find("Targets") == std::string::npos);
}

// The lean sweep shipped switched off pending verification, so its switch
// follows the mod's default, which is on; the rest of [Collision] is carried.
void AnOldCollisionSwitchFollowsTheDefault() {
    Scratch s("collision");
    WriteFileBytes(s.legacy(), "[Collision]\r\nEnabled=0\r\nRadius=20.5\r\nChannel=3\r\nReleaseSmoothing=0.5\r\n");
    const tow_ht::Config c = s.Load();
    CHECK(c.collision_enabled);
    CHECK(c.collision_margin == 20.5f);
    CHECK(c.collision_channel == 3);
    CHECK(c.collision_release_smoothing == 0.5f);
    const std::string migrated = ReadFileBytes(s.ini());
    CHECK(Holds(migrated, "CollisionEnabled=default"));
    CHECK(Holds(migrated, "CollisionMargin=20.5"));
    CHECK(Holds(migrated, "CollisionChannel=3"));
    CHECK(Holds(migrated, "CollisionReleaseSmoothing=0.5"));
}

// The keys that only this game has keep their values under their new names.
void TheGameKeysAreCarried() {
    Scratch s("local");
    WriteFileBytes(s.legacy(), "[General]\r\nCenterWindow=0\r\n[Aim]\r\nTraceChannel=2\r\nMaxDistance=5000\r\n"
                               "[Dev]\r\nWidgetDump=1\r\nWidgetDumpOuter=HUD_BP_C\r\nPoseLog=1\r\n"
                               "[Diag]\r\nInjectMode=0\r\n");
    const tow_ht::Config c = s.Load();
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

    TheCommittedFileIsTheFreshRender();
    TheCommittedFileHoldsDefaultOnEveryGlobalRow();
    FirstLaunchCreatesTheCommittedFile();
    DefaultRowsFollowDefaultsIni();
    TheYawToggleSavesItsLineAndNothingElse();
    TheModeCycleSavesThePair();
    AnOldFileIsImportedAndLeftAsItWas();
    AnImportedValueEqualToDefaultsIniIsWrittenAsDefault();
    AnOldYawKeyJoinsItsChord();
    AnOldPositionSwitchIsTheStartupMode();
    AChangedSensitivityIsNotCarried();
    TheOldReticleSettingsAreNotCarried();
    AnOldCollisionSwitchFollowsTheDefault();
    TheGameKeysAreCarried();

    return tow_test::Report();
}

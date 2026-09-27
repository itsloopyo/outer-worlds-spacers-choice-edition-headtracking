// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The differential test for the import of HeadTracking.ini into CameraUnlock.ini.
//
// Three readings of every input, and what may differ between them:
//
//   Oracle     the reader of the newest published build (v0.1.0, 1014f66, core
//              c480d8a), compiled from its own sources (oracle_api.h), and the
//              startup state and bindings it made of the reading, read from
//              that build's source text
//   Import     the frozen reader in src/legacy_config/
//   Migration  the config owner, in a folder holding only the input as
//              HeadTracking.ini, importing it into a new CameraUnlock.ini, then
//              the canonical reader and table on what it wrote
//
// Comparison 1, oracle against import, is what a player sees change that the
// conversion did not cause: commits since the published build that change how
// the file is read. Each one is listed below with its commit.
//
// Comparison 2, import against migration, is the proof for the conversion. The
// differences it allows are the approved changes the import records: a
// sensitivity or inversion set away from the shipped value (pose_shaping), the
// crosshair switch and widget list (reticle), and the lean sweep's switch, which
// shipped off pending verification and now takes the mod's default, on
// (follows_default). That switch is also the one default the conversion moved,
// from off to the schema's built-in on, so the no-file input differs there too.
// A [Diag] InjectMode of 0 handed every caller the head pose, which coupled the
// aim to the head, so it imports as -1, the build's own choice (coupled_aim).
//
// A legacy yaw key on a Ctrl, Shift or Alt key alone imports as unbound and
// keeps its Ctrl+Shift+H chord (normalisation N3).
//
// A row the player never changed from what the frozen build shipped follows
// Defaults.ini: the import lists it in follows_defaults_ini and the migration
// writes it `default`, the tracking mode pair as one unit. The test derives that
// list from what the import read, a row whose every observed value is the
// shipped one, and holds the import's list to it on every input. The toggle and
// mode cycle keys were bound in code, so they always follow it.
//
// Each input is migrated three times: over a Defaults.ini at the built-in
// values, with the legacy file read-only, and over a Defaults.ini that differs
// from the built-in values on every global row. Over the first two each
// migration must run on what the import read; over the third a row the player
// never changed is `default` and takes Defaults.ini's value, and a changed row
// keeps the player's. The next start must read CameraUnlock.ini and write
// nothing. HeadTracking.ini keeps its bytes, write time and attributes
// throughout, and the folder holds it and CameraUnlock.ini and nothing else.
//
// Inputs: the published build's first-run file (it shipped no config and seeded
// none, so every player's file started as that one), no file, an empty file,
// and core's corpus of mutations of the first-run file.
//
// The distinct migrated files are written beside the executable under
// migrated\, for lint-migrated.mjs to run core's canonical config lint over.

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "config.h"
#include "crosshair_widgets.h"
#include "legacy_config/legacy_config.h"
#include "legacy_config/legacy_import.h"
#include "oracle_api.h"
#include "test_harness.h"

#include "cameraunlock/config/canonical_ini.h"
#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

namespace {

using cameraunlock::TrackingMode;
namespace cfg = cameraunlock::config;
using cfg::schema::Concept;
namespace testing = cameraunlock::config::testing;

// ---- Provenance ------------------------------------------------------------
//
// Every source the oracle and the import compile or read, pinned by the SHA-256
// of its bytes. The oracle's files are the published build's, taken with
// `git show v0.1.0:src/<file>` and `git -C cameraunlock-core show c480d8a:<path>`.
// The core files both readers compile are hash-equal to c480d8a's, and so are
// src/logging.h, src/inject_mode.h and src/legacy_config/config_sanitize.h,
// which is v0.1.0's src/config_sanitize.h moved. So the readers differ only
// where the mod's own reader changed. The frozen reader is pinned at the commit
// that froze it. The map in legacy_import.cpp is pinned too and moved twice, by
// the owner's rule of 2026-09-26 that a setting the player never changed
// follows Defaults.ini, with N3, and by the ruling of the same day that aim is
// always decoupled, which drops a [Diag] InjectMode of 0.

struct Pinned {
    const char* path;
    const char* sha256;
};

constexpr Pinned kPinned[] = {
    // The oracle: v0.1.0:src/...
    {"tests/config_differential/oracle/src/config.cpp", "668b135941ca2c4735bfb26490faae69614d24bcf4cfe844dce60a24a94d4747"},
    {"tests/config_differential/oracle/src/config.h", "afd682e6d8084e5f96796b7c195167de776bb0b3a5fda5d482ebe46f1eac5856"},
    {"tests/config_differential/oracle/src/ads.h", "b778ed7d96eef9db39bca2cb385a3a6b3daf9516cfc5d552acce1bb29abf22f6"},
    {"tests/config_differential/oracle/src/config_sanitize.h", "d579938044e5b2d55864b70017f29a72aedd32100f94cc91fce33a254161c06c"},
    {"tests/config_differential/oracle/src/inject_mode.h", "81578f46ae65c45fd8957553f3ffa88a63170f90384faedc979a0eca1ee49c36"},
    {"tests/config_differential/oracle/src/logging.h", "72f187e5fce8934358a9da57863237f7e3a4e5e80e437e0a7608d72141da584c"},
    // The oracle: c480d8a:cpp/include/cameraunlock/ads/..., which core has changed or removed since.
    {"tests/config_differential/oracle/core/cameraunlock/ads/ads_blend.h", "bcc009fa97e0d8284a46ed5284ec0741ad3f1e8d1babe4be07bf45476351560a"},
    {"tests/config_differential/oracle/core/cameraunlock/ads/ads_fade.h", "00b80e59d261546dd50138759676f5a1b0d07681fa79a9b89be69797dc35c37f"},
    {"tests/config_differential/oracle/core/cameraunlock/ads/ads_mode.h", "94cd36b585e878e673566f602e9496417cb797970162fcc9c2af1e50e24358de"},
    {"tests/config_differential/oracle/core/cameraunlock/ads/entry_pose.h", "0c26c26fd3f6ba8307b731af391340e9286504ef157329c04883495f211cc870"},
    // The oracle's startup and bindings, read as text: v0.1.0:src/...
    {"tests/config_differential/oracle/uncompiled/headtracking_mod.cpp", "5fbe8bfbfe21af0154e574a6aed9c3d59ac369859fc71ed9b8c28caecae5ceb4"},
    {"tests/config_differential/oracle/uncompiled/mod_hotkeys.cpp", "d9073b126445ad74b1c5496367d6af5fde0d887ee1d0d0c509ad5ef2bf0f289b"},
    // Both readers: core at the pin, hash-equal to c480d8a:cpp/...
    {"cameraunlock-core/cpp/include/cameraunlock/config/ini_reader.h", "a7ffb44210ff59672fa97e8e5feaa2cb3e81938fcc0334a384c68bc371b3857a"},
    {"cameraunlock-core/cpp/src/config/ini_reader.cpp", "e01515c2656aaf533bae4350dc45b702c3e3d4043935743dcc9bd5ea581fbe1c"},
    {"cameraunlock-core/cpp/include/cameraunlock/logging/file_log.h", "43bdd2ef8554c78e5f440333463750c13b95110fe672b0b6e273244df9e7d169"},
    {"cameraunlock-core/cpp/src/logging/file_log.cpp", "73c53c2baa06bbfebe8211f62678aa2b60cb95f604743d3686951ba56b87ea47"},
    {"cameraunlock-core/cpp/include/cameraunlock/data/position_settings.h", "b24dceb8e25475aebc5a468a5c7362a4a4e64204d183d1408525345f32f547f5"},
    {"cameraunlock-core/cpp/include/cameraunlock/math/smoothing_utils.h", "fc2146f8c585e5f610c7234e302f59de4945679cfa28ff479ca47477ec073f22"},
    {"cameraunlock-core/cpp/include/cameraunlock/math/angle_utils.h", "d7a905270933e3cb0c4c361d29d3fd701655498cbcd1875ea79d180468bdbe6a"},
    {"cameraunlock-core/cpp/include/cameraunlock/math/finite_utils.h", "c59772d698d54ade3374ee1221b74f5563a86d76f0eb34a989ef7efab389c0ad"},
    // The import: src/logging.h and src/inject_mode.h are v0.1.0's, the legacy
    // folder is frozen.
    {"src/logging.h", "72f187e5fce8934358a9da57863237f7e3a4e5e80e437e0a7608d72141da584c"},
    {"src/inject_mode.h", "81578f46ae65c45fd8957553f3ffa88a63170f90384faedc979a0eca1ee49c36"},
    {"src/legacy_config/config_sanitize.h", "d579938044e5b2d55864b70017f29a72aedd32100f94cc91fce33a254161c06c"},
    {"src/legacy_config/legacy_config.h", "674c3f6b14bf663246347d8ea00f8b52d0b84b36d510eeccb70937c0fde9d3ed"},
    {"src/legacy_config/legacy_import.h", "efcb39399acb55fd08ee92b2f8bb8ee52525355ccec89f5ac0bab5d1119155d0"},
    {"src/legacy_config/legacy_import.cpp", "891515a32bc33a0c1346b7d4b5a931d554e323f93d796e9ecec2e3ee5ccc2316"},
    {"src/legacy_config/legacy_config.cpp", "6841b38f9f69c9113827ca291519c9319ea7ffd06db4a46e613069e52cbf0cb8"},
};

std::string ReadFileBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string Sha256Hex(const std::string& bytes) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        throw std::runtime_error("BCryptOpenAlgorithmProvider(SHA256) failed");
    }
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32] = {};
    const bool ok =
        BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0)) &&
        BCRYPT_SUCCESS(BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
                                      static_cast<ULONG>(bytes.size()), 0)) &&
        BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0));
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok) throw std::runtime_error("SHA-256 failed");
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    for (unsigned char b : digest) {
        out += kHex[b >> 4];
        out += kHex[b & 15];
    }
    return out;
}

void SourcesAreThePinnedOnes() {
    for (const Pinned& p : kPinned) {
        const std::string actual = Sha256Hex(ReadFileBytes(std::string(TOW_SOURCE_DIR) + "/" + p.path));
        if (actual != p.sha256) std::printf("  %s is %s\n", p.path, actual.c_str());
        CHECK_MSG(actual == p.sha256, p.path);
    }
}

// ---- Scratch folders ---------------------------------------------------------
//
// One folder per input: GetPrivateProfileString, which both readers sit on, is
// free to cache the file it last read. Defaults.ini goes in a folder beside it,
// so the game folder holds only the files the mod writes there.

void DeleteFolder(const std::string& dir) {
    WIN32_FIND_DATAA found;
    const HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &found);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const std::string file = dir + "\\" + found.cFileName;
            SetFileAttributesA(file.c_str(), FILE_ATTRIBUTE_NORMAL);
            DeleteFileA(file.c_str());
        } while (FindNextFileA(h, &found));
        FindClose(h);
    }
    RemoveDirectoryA(dir.c_str());
}

void WriteBytes(const std::string& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("cannot write " + path);
}

class Scratch {
public:
    Scratch() {
        static unsigned s_next = 0;
        char temp[MAX_PATH] = {};
        GetTempPathA(MAX_PATH, temp);
        dir_ = std::string(temp) + "tow_ht_diff_" + std::to_string(GetCurrentProcessId()) + "_" +
               std::to_string(s_next++);
        if (!CreateDirectoryA(dir_.c_str(), nullptr)) {
            throw std::runtime_error("cannot create " + dir_ + ", error " + std::to_string(GetLastError()));
        }
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    ~Scratch() {
        DeleteFolder(dir_);
        DeleteFolder(global());
    }

    const std::string& dir() const { return dir_; }
    std::wstring wdir() const { return std::wstring(dir_.begin(), dir_.end()); }
    // The legacy file, which is the input as the frozen readers read it.
    std::string ini() const { return dir_ + "\\HeadTracking.ini"; }
    std::wstring wini() const {
        const std::string path = ini();
        return std::wstring(path.begin(), path.end());
    }
    std::string canonical() const { return dir_ + "\\CameraUnlock.ini"; }
    std::string global() const { return dir_ + "_global"; }
    std::string defaults_ini() const { return global() + "\\Defaults.ini"; }
    cfg::DefaultsFile defaults() const {
        const std::string path = defaults_ini();
        return cfg::DefaultsFile::At(std::wstring(path.begin(), path.end()));
    }

    // The names of the files in the folder.
    std::vector<std::string> Names() const {
        std::vector<std::string> names;
        WIN32_FIND_DATAA found;
        const HANDLE h = FindFirstFileA((dir_ + "\\*").c_str(), &found);
        if (h == INVALID_HANDLE_VALUE) return names;
        do {
            if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) names.push_back(found.cFileName);
        } while (FindNextFileA(h, &found));
        FindClose(h);
        std::sort(names.begin(), names.end());
        return names;
    }

    void Write(const std::string& bytes) const { WriteBytes(ini(), bytes); }

    // Defaults.ini as a player left it, before the first launch.
    void WriteDefaults(const std::string& bytes) const {
        if (!CreateDirectoryA(global().c_str(), nullptr)) throw std::runtime_error("cannot create " + global());
        WriteBytes(defaults_ini(), bytes);
    }

private:
    std::string dir_;
};

// A file's bytes, last write time and attributes.
struct FileState {
    bool present = false;
    std::string bytes;
    std::uint64_t written = 0;
    DWORD attributes = 0;

    bool operator==(const FileState& o) const {
        return present == o.present && bytes == o.bytes && written == o.written && attributes == o.attributes;
    }
};

FileState StateOf(const std::string& path) {
    FileState state;
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data)) return state;
    state.present = true;
    state.bytes = ReadFileBytes(path);
    state.written = (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) |
                    data.ftLastWriteTime.dwLowDateTime;
    state.attributes = data.dwFileAttributes;
    return state;
}

// ---- What a reading does -------------------------------------------------------

std::uint32_t Bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

enum Action { kToggle, kCycleMode, kYawMode, kCycleInject, kAdsMode };

// One registered binding: the action, the virtual-key code, and the modifiers
// it needs (0, or Ctrl+Shift as cameraunlock::input::KeyModifiers spells it).
using Hotkey = std::tuple<int, int, unsigned>;
constexpr unsigned kPlain = 0;
constexpr unsigned kCtrlShift = 3;

// Everything a reading decides that the running mod acts on: the settings, the
// state at startup, and the bindings the poller registers.
struct Observed {
    int udp_port = 0;
    bool start_enabled = false;
    bool start_world_yaw = false;
    int start_mode = 0;
    bool center_window = false;
    float local_smoothing = 0;
    float remote_smoothing = 0;
    // X, Y up, Y down, Z forward, Z back, as the position processor holds them.
    float limits[5] = {};
    bool crosshair_follows = false;
    std::string crosshair_widgets;
    int aim_trace_channel = 0;
    float aim_trace_distance = 0;
    bool collision_enabled = false;
    float collision_margin = 0;
    int collision_channel = 0;
    float collision_release_smoothing = 0;
    bool widget_dump = false;
    std::string widget_dump_outer;
    bool pose_log = false;
    int inject_mode = 0;
    std::vector<Hotkey> hotkeys;
};

// The sensitivities and inversions a legacy reader read, which the canonical
// format no longer has.
struct PoseShaping {
    float sensitivity[3] = {};
    bool invert[3] = {};
    float position_sensitivity[3] = {};
};

std::vector<std::string> Differences(const Observed& a, const Observed& b) {
    std::vector<std::string> out;
    if (a.udp_port != b.udp_port) out.push_back("UDP port");
    if (a.start_enabled != b.start_enabled) out.push_back("tracking on at startup");
    if (a.start_world_yaw != b.start_world_yaw) out.push_back("yaw mode at startup");
    if (a.start_mode != b.start_mode) out.push_back("tracking mode at startup");
    if (a.center_window != b.center_window) out.push_back("window centring");
    if (Bits(a.local_smoothing) != Bits(b.local_smoothing)) out.push_back("local smoothing");
    if (Bits(a.remote_smoothing) != Bits(b.remote_smoothing)) out.push_back("remote smoothing");
    static const char* const kLimits[] = {"limit x", "limit y up", "limit y down", "limit z forward", "limit z back"};
    for (int i = 0; i < 5; ++i) {
        if (Bits(a.limits[i]) != Bits(b.limits[i])) out.push_back(kLimits[i]);
    }
    if (a.crosshair_follows != b.crosshair_follows) out.push_back("crosshair follows the aim");
    if (a.crosshair_widgets != b.crosshair_widgets) out.push_back("crosshair widgets");
    if (a.aim_trace_channel != b.aim_trace_channel) out.push_back("aim trace channel");
    if (Bits(a.aim_trace_distance) != Bits(b.aim_trace_distance)) out.push_back("aim trace distance");
    if (a.collision_enabled != b.collision_enabled) out.push_back("lean collision");
    if (Bits(a.collision_margin) != Bits(b.collision_margin)) out.push_back("collision margin");
    if (a.collision_channel != b.collision_channel) out.push_back("collision channel");
    if (Bits(a.collision_release_smoothing) != Bits(b.collision_release_smoothing)) {
        out.push_back("collision release smoothing");
    }
    if (a.widget_dump != b.widget_dump) out.push_back("widget dump");
    if (a.widget_dump_outer != b.widget_dump_outer) out.push_back("widget dump outer");
    if (a.pose_log != b.pose_log) out.push_back("pose log");
    if (a.inject_mode != b.inject_mode) out.push_back("inject mode");
    if (a.hotkeys != b.hotkeys) out.push_back("hotkeys");
    return out;
}

bool SamePoseShaping(const PoseShaping& a, const PoseShaping& b) {
    for (int i = 0; i < 3; ++i) {
        if (Bits(a.sensitivity[i]) != Bits(b.sensitivity[i])) return false;
        if (a.invert[i] != b.invert[i]) return false;
        if (Bits(a.position_sensitivity[i]) != Bits(b.position_sensitivity[i])) return false;
    }
    return true;
}

// ---- The published build's startup and bindings ------------------------------
//
// The oracle library compiles only v0.1.0's reader: its ApplyConfigToSession and
// its hotkey Register pull in the session, the view hook and the poller. The
// oracle reads what those two do from their source instead, the pinned copies
// in oracle/uncompiled/, and throws on any statement it does not recognise, so
// an unread line cannot pass as agreement.

std::string CollapseWhitespace(const std::string& s) {
    std::string out;
    bool space = false;
    for (const char c : s) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            space = !out.empty();
            continue;
        }
        if (space) out += ' ';
        space = false;
        out += c;
    }
    return out;
}

// The definition that starts with `signature` in oracle/uncompiled/<file>, up to
// its closing brace in column 0, line comments removed and whitespace collapsed
// to single spaces.
std::string PublishedBody(const char* file, const char* signature) {
    const std::string src =
        ReadFileBytes(std::string(TOW_SOURCE_DIR) + "/tests/config_differential/oracle/uncompiled/" + file);
    const std::size_t start = src.find(signature);
    if (start == std::string::npos || src.find(signature, start + 1) != std::string::npos) {
        throw std::runtime_error(std::string(file) + " does not define " + signature + " exactly once");
    }
    const std::size_t end = src.find("\n}\n", start);
    if (end == std::string::npos) throw std::runtime_error(std::string(file) + ": " + signature + " has no end");
    const std::string body = src.substr(start, end + 2 - start);
    std::string code;
    for (std::size_t i = 0; i < body.size();) {
        if (body.compare(i, 2, "//") == 0) {
            i = body.find('\n', i);
            if (i == std::string::npos) break;
            continue;
        }
        code += body[i++];
    }
    return CollapseWhitespace(code);
}

std::size_t Occurrences(const std::string& s, const std::string& what) {
    std::size_t n = 0;
    for (std::size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + 1)) ++n;
    return n;
}

int TrackingModeNamed(const std::string& name) {
    if (name == "RotationAndPosition") return static_cast<int>(TrackingMode::RotationAndPosition);
    if (name == "RotationOnly") return static_cast<int>(TrackingMode::RotationOnly);
    if (name == "PositionOnly") return static_cast<int>(TrackingMode::PositionOnly);
    throw std::runtime_error("unknown tracking mode " + name);
}

// What v0.1.0 does with the reader's output at startup: the session mode for
// each value of [Position] Enabled, and the reader field each of the position
// processor's five limits takes.
struct PublishedStartup {
    int mode_if_position = 0;
    int mode_if_no_position = 0;
    // X, Y up, Y down, Z forward, Z back, as the position processor holds them.
    std::string limit_fields[5];
};

const PublishedStartup& Startup() {
    static const PublishedStartup s_startup = [] {
        const std::string body = PublishedBody("headtracking_mod.cpp", "void ApplyConfigToSession() {");
        PublishedStartup p;

        const std::regex set_mode(
            R"(g_session->SetMode\(g_config\.position_enabled \? TrackingMode::(\w+) : TrackingMode::(\w+)\);)");
        std::smatch m;
        if (Occurrences(body, "SetMode(") != 1 || !std::regex_search(body, m, set_mode)) {
            throw std::runtime_error("v0.1.0 ApplyConfigToSession: the SetMode call is not the one this test reads");
        }
        p.mode_if_position = TrackingModeNamed(m[1].str());
        p.mode_if_no_position = TrackingModeNamed(m[2].str());

        static const char* const kProcessor[5] = {"limit_x", "limit_y", "limit_y_down", "limit_z", "limit_z_back"};
        const std::regex limit(R"(ps\.(limit_\w+) = g_config\.(\w+);)");
        std::size_t read = 0;
        for (std::sregex_iterator it(body.begin(), body.end(), limit), done; it != done; ++it, ++read) {
            const std::string target = (*it)[1].str();
            const auto slot = std::find_if(std::begin(kProcessor), std::end(kProcessor),
                                           [&](const char* name) { return target == name; });
            if (slot == std::end(kProcessor)) throw std::runtime_error("v0.1.0 sets unknown ps." + target);
            std::string& field = p.limit_fields[slot - std::begin(kProcessor)];
            if (!field.empty()) throw std::runtime_error("v0.1.0 sets ps." + target + " twice");
            field = (*it)[2].str();
        }
        if (read != std::size(kProcessor) || Occurrences(body, "ps.limit_") != read) {
            throw std::runtime_error("v0.1.0 ApplyConfigToSession: the limits are not the five assignments this test reads");
        }
        return p;
    }();
    return s_startup;
}

float PublishedLimitField(const tow_oracle::PublishedConfig& c, const std::string& field) {
    if (field == "limit_x") return c.limit_x;
    if (field == "limit_y") return c.limit_y;
    if (field == "limit_z") return c.limit_z;
    if (field == "limit_z_back") return c.limit_z_back;
    throw std::runtime_error("v0.1.0 sets a limit from g_config." + field + ", which this test does not read");
}

// v0.1.0's Register, binding by binding: the action, the key (-1 for
// config.yaw_mode_key) and its modifiers. NavGuarded is kPlain and ChordGuarded
// kCtrlShift because that is how key_binding_registration.h fires a binding
// with no modifiers and one naming Ctrl+Shift.
const std::vector<Hotkey>& PublishedBindings() {
    static const std::vector<Hotkey> s_bindings = [] {
        const std::string body =
            PublishedBody("mod_hotkeys.cpp", "bool Register(Session& session, const Config& config) {");
        const std::regex add(
            R"(g_poller->AddHotkey\((0x[0-9A-Fa-f]+|config\.yaw_mode_key)(?: /\*[^*]*\*/)?, (NavGuarded|ChordGuarded)\(\[\] \{ (\w+)\(\); \}\)\);)");
        std::vector<Hotkey> bindings;
        for (std::sregex_iterator it(body.begin(), body.end(), add), done; it != done; ++it) {
            const std::string key = (*it)[1].str();
            const int vk = key == "config.yaw_mode_key" ? -1 : std::stoi(key, nullptr, 16);
            const unsigned modifiers = (*it)[2].str() == "NavGuarded" ? kPlain : kCtrlShift;
            const std::string handler = (*it)[3].str();
            int action;
            if (handler == "ToggleTracking") action = kToggle;
            else if (handler == "CycleTrackingMode") action = kCycleMode;
            else if (handler == "ToggleYawMode") action = kYawMode;
            else if (handler == "CycleAdsMode") action = kAdsMode;
            else if (handler == "CycleInject") action = kCycleInject;
            else throw std::runtime_error("v0.1.0 binds unknown handler " + handler);
            bindings.push_back({action, vk, modifiers});
        }
        if (bindings.empty() || Occurrences(body, "AddHotkey(") != bindings.size()) {
            throw std::runtime_error("v0.1.0 Register: a binding is not in the shape this test reads");
        }
        return bindings;
    }();
    return s_bindings;
}

// ---- The frozen commit's startup and bindings ---------------------------------
//
// Hand copied from the frozen commit (20e7260), whose ApplyConfigToSession and
// Register the import's reading runs on. Comparison 1 holds each to the
// published build's, read above, so a mistake here fails it.

// src/headtracking_mod.cpp ApplyConfigToSession: [Position] Enabled picks the
// startup mode and nothing else.
int LegacyStartMode(bool position_enabled) {
    return static_cast<int>(position_enabled ? TrackingMode::RotationAndPosition : TrackingMode::RotationOnly);
}

// src/headtracking_mod.cpp ApplyConfigToSession: the one vertical limit is
// mirrored into the downward bound.
void LegacyLimits(float x, float y, float z, float z_back, float (&out)[5]) {
    const float limits[5] = {x, y, y, z, z_back};
    std::copy(std::begin(limits), std::end(limits), out);
}

// src/mod_hotkeys.cpp Register, which is v0.1.0's less its ADS lines: End, Page
// Up and the configured yaw key NavGuarded, the Y/G/H chords and the J inject
// chord ChordGuarded.
std::vector<Hotkey> LegacyHotkeys(int yaw_mode_key) {
    std::vector<Hotkey> keys = {
        {kToggle, 0x23, kPlain},     {kCycleMode, 0x21, kPlain},     {kYawMode, yaw_mode_key, kPlain},
        {kToggle, 0x59, kCtrlShift}, {kCycleMode, 0x47, kCtrlShift}, {kYawMode, 0x48, kCtrlShift},
        {kCycleInject, 0x4A, kCtrlShift},
    };
    std::sort(keys.begin(), keys.end());
    return keys;
}

struct OracleReading {
    Observed observed;
    PoseShaping pose;
    int ads_mode = 0;
};

OracleReading ReadOracle(const std::string& dir) {
    const tow_oracle::PublishedConfig c = tow_oracle::Load(dir);
    OracleReading r;
    Observed& o = r.observed;
    o.udp_port = c.udp_port;
    // v0.1.0:src/view_hook.cpp:523-524 (Install).
    o.start_enabled = c.enable_on_startup;
    o.start_world_yaw = c.world_space_yaw;
    const PublishedStartup& startup = Startup();
    o.start_mode = c.position_enabled ? startup.mode_if_position : startup.mode_if_no_position;
    o.center_window = c.center_window;
    o.local_smoothing = c.local_smoothing;
    o.remote_smoothing = c.remote_smoothing;
    for (int i = 0; i < 5; ++i) o.limits[i] = PublishedLimitField(c, startup.limit_fields[i]);
    o.crosshair_follows = c.reticle_enabled;
    o.crosshair_widgets = c.reticle_targets;
    o.aim_trace_channel = c.aim_trace_channel;
    o.aim_trace_distance = c.aim_trace_distance;
    o.collision_enabled = c.collision_enabled;
    o.collision_margin = c.collision_radius;
    o.collision_channel = c.collision_channel;
    o.collision_release_smoothing = c.collision_release_smoothing;
    o.widget_dump = c.widget_dump;
    o.widget_dump_outer = c.widget_dump_outer;
    o.pose_log = c.pose_log;
    o.inject_mode = c.inject_mode;
    for (Hotkey h : PublishedBindings()) {
        if (std::get<1>(h) == -1) std::get<1>(h) = c.yaw_mode_key;
        o.hotkeys.push_back(h);
    }
    std::sort(o.hotkeys.begin(), o.hotkeys.end());
    r.pose = {{c.yaw_sensitivity, c.pitch_sensitivity, c.roll_sensitivity},
              {c.invert_yaw, c.invert_pitch, c.invert_roll},
              {c.position_sensitivity_x, c.position_sensitivity_y, c.position_sensitivity_z}};
    r.ads_mode = c.ads_mode;
    return r;
}

Observed ObserveLegacy(const tow_ht::legacy::Config& c) {
    Observed o;
    o.udp_port = c.udp_port;
    o.start_enabled = c.enable_on_startup;
    o.start_world_yaw = c.world_space_yaw;
    o.start_mode = LegacyStartMode(c.position_enabled);
    o.center_window = c.center_window;
    o.local_smoothing = c.local_smoothing;
    o.remote_smoothing = c.remote_smoothing;
    LegacyLimits(c.limit_x, c.limit_y, c.limit_z, c.limit_z_back, o.limits);
    o.crosshair_follows = c.reticle_enabled;
    o.crosshair_widgets = c.reticle_targets;
    o.aim_trace_channel = c.aim_trace_channel;
    o.aim_trace_distance = c.aim_trace_distance;
    o.collision_enabled = c.collision_enabled;
    o.collision_margin = c.collision_radius;
    o.collision_channel = c.collision_channel;
    o.collision_release_smoothing = c.collision_release_smoothing;
    o.widget_dump = c.widget_dump;
    o.widget_dump_outer = c.widget_dump_outer;
    o.pose_log = c.pose_log;
    o.inject_mode = c.inject_mode;
    o.hotkeys = LegacyHotkeys(c.yaw_mode_key);
    return o;
}

PoseShaping LegacyPoseShapingOf(const tow_ht::legacy::Config& c) {
    return {{c.yaw_sensitivity, c.pitch_sensitivity, c.roll_sensitivity},
            {c.invert_yaw, c.invert_pitch, c.invert_roll},
            {c.position_sensitivity_x, c.position_sensitivity_y, c.position_sensitivity_z}};
}

// ---- Rows that follow Defaults.ini -----------------------------------------------

// Every global row the table binds, each of which follows Defaults.ini.
const std::set<Concept>& AllRows() {
    static const std::set<Concept> all = {
        Concept::UdpPort,          Concept::EnableOnStartup,
        Concept::WorldSpaceYaw,    Concept::RotationEnabled,
        Concept::PositionEnabled,  Concept::LocalSmoothing,
        Concept::RemoteSmoothing,  Concept::PositionLimitX,
        Concept::PositionLimitY,   Concept::PositionLimitYDown,
        Concept::PositionLimitZ,   Concept::PositionLimitZBack,
        Concept::CollisionEnabled, Concept::CollisionReleaseSmoothing,
        Concept::ToggleKey,        Concept::CycleTrackingModeKey,
        Concept::YawModeKey,
    };
    return all;
}

std::vector<Hotkey> BindingsOf(const std::vector<Hotkey>& hotkeys, int action) {
    std::vector<Hotkey> out;
    for (const Hotkey& h : hotkeys) {
        if (std::get<0>(h) == action) out.push_back(h);
    }
    return out;
}

// The rows the player never changed: every value the row stands for reads as it
// does with no file, the frozen build's defaults. The mode pair is both rows or
// neither.
std::set<Concept> UntouchedRows(const Observed& read) {
    const Observed shipped = ObserveLegacy(tow_ht::legacy::Config{});
    std::set<Concept> changed;
    if (read.udp_port != shipped.udp_port) changed.insert(Concept::UdpPort);
    if (read.start_enabled != shipped.start_enabled) changed.insert(Concept::EnableOnStartup);
    if (read.start_world_yaw != shipped.start_world_yaw) changed.insert(Concept::WorldSpaceYaw);
    if (read.start_mode != shipped.start_mode) {
        changed.insert(Concept::RotationEnabled);
        changed.insert(Concept::PositionEnabled);
    }
    if (Bits(read.local_smoothing) != Bits(shipped.local_smoothing)) changed.insert(Concept::LocalSmoothing);
    if (Bits(read.remote_smoothing) != Bits(shipped.remote_smoothing)) changed.insert(Concept::RemoteSmoothing);
    const Concept limits[5] = {Concept::PositionLimitX, Concept::PositionLimitY, Concept::PositionLimitYDown,
                               Concept::PositionLimitZ, Concept::PositionLimitZBack};
    for (int i = 0; i < 5; ++i) {
        if (Bits(read.limits[i]) != Bits(shipped.limits[i])) changed.insert(limits[i]);
    }
    if (read.collision_enabled != shipped.collision_enabled) changed.insert(Concept::CollisionEnabled);
    if (Bits(read.collision_release_smoothing) != Bits(shipped.collision_release_smoothing)) {
        changed.insert(Concept::CollisionReleaseSmoothing);
    }
    const std::pair<int, Concept> keys[] = {
        {kToggle, Concept::ToggleKey}, {kCycleMode, Concept::CycleTrackingModeKey}, {kYawMode, Concept::YawModeKey}};
    for (const auto& [action, row] : keys) {
        if (BindingsOf(read.hotkeys, action) != BindingsOf(shipped.hotkeys, action)) changed.insert(row);
    }
    std::set<Concept> untouched;
    for (const Concept row : AllRows()) {
        if (!changed.count(row)) untouched.insert(row);
    }
    return untouched;
}

std::string Names(const std::set<Concept>& rows) {
    std::string text;
    for (const Concept row : rows) {
        text += (text.empty() ? "" : ", ") + std::string(cfg::schema::kConcepts[static_cast<std::size_t>(row)].name);
    }
    return text.empty() ? "none" : text;
}

// ---- Inputs --------------------------------------------------------------------

std::string DataPath(const char* name) {
    return std::string(TOW_SOURCE_DIR) + "/tests/config_differential/data/" + name;
}

// The published build's first-run file, extracted once from the oracle's
// WriteDefaultIfMissing and committed.
std::string FirstRunFile() { return ReadFileBytes(DataPath("v0.1.0-first-run.ini")); }

// The generator refuses the call when these and the descriptors name different
// keys, so the corpus covers every key the import reads.
std::vector<cfg::LegacyKey> CorpusReads() { return tow_ht::legacy::Import().keys; }

// How the corpus varies each key the frozen reader reads. The out-of-range
// values sit either side of the range each key is clamped or refused to.
std::vector<testing::MutationKey> CorpusKeys() {
    const std::vector<std::string> sensitivity = {"0.05", "3.5"};
    const std::vector<std::string> position_sensitivity = {"-1.0", "6.0"};
    const std::vector<std::string> limit = {"0.001", "0.6"};
    const std::vector<std::string> smoothing = {"-0.5", "1.5"};
    const std::vector<std::string> channel = {"-1", "256"};
    return {
        {"Network", "UdpPort", "5000", {"80", "70000"}},
        {"General", "EnableOnStartup", "0", {}},
        {"General", "WorldSpaceYaw", "0", {}},
        {"General", "CenterWindow", "0", {}},
        {"Rotation", "YawSensitivity", "1.5", sensitivity},
        {"Rotation", "PitchSensitivity", "0.5", sensitivity},
        {"Rotation", "RollSensitivity", "2.25", sensitivity},
        {"Rotation", "InvertYaw", "1", {}},
        {"Rotation", "InvertPitch", "1", {}},
        {"Rotation", "InvertRoll", "1", {}},
        {"Rotation", "LocalSmoothing", "0.3", smoothing},
        {"Rotation", "RemoteSmoothing", "0.6", smoothing},
        {"Position", "Enabled", "0", {}},
        {"Position", "SensitivityX", "2.0", position_sensitivity},
        {"Position", "SensitivityY", "0.5", position_sensitivity},
        {"Position", "SensitivityZ", "1.25", position_sensitivity},
        {"Position", "LimitX", "0.25", limit},
        {"Position", "LimitY", "0.35", limit},
        {"Position", "LimitZ", "0.45", limit},
        {"Position", "LimitZBack", "0.05", limit},
        {"Reticle", "Enabled", "0", {}},
        {"Reticle", "Targets", "Crosshair@Reticle/WidgetTree/HUD_BP_C/IndianaGameInstance", {}},
        {"Aim", "TraceChannel", "2", channel},
        {"Aim", "MaxDistance", "5000", {"0.5", "2000000"}},
        {"Collision", "Enabled", "1", {}},
        {"Collision", "Radius", "20.5", {"5", "250"}},
        {"Collision", "Channel", "3", channel},
        {"Collision", "ReleaseSmoothing", "0.5", smoothing},
        {"Dev", "WidgetDump", "1", {}},
        {"Dev", "WidgetDumpOuter", "HUD_BP_C", {}},
        {"Dev", "PoseLog", "1", {}},
        {"Diag", "InjectMode", "0", {"-2", "18"}},
        {"Hotkeys", "YawModeKey", "0x2E", {"0x1FF"}, true},
    };
}

struct Input {
    std::string name;
    bool present;
    std::string bytes;
};

std::vector<Input> Inputs() {
    std::vector<Input> inputs = {
        {"v0.1.0 first-run file", true, FirstRunFile()},
        {"no file", false, {}},
        {"empty file", true, {}},
    };
    // N3: a yaw key the frozen reader accepts that is a Ctrl, Shift or Alt key
    // alone.
    for (const char* code : {"0x10", "0x11", "0x12", "0xA0", "0xA5"}) {
        std::string bytes = FirstRunFile();
        const std::string shipped = "YawModeKey=0x22";
        const std::size_t at = bytes.find(shipped);
        if (at == std::string::npos) throw std::runtime_error("the first-run file has no YawModeKey=0x22");
        bytes.replace(at, shipped.size(), std::string("YawModeKey=") + code);
        inputs.push_back({std::string("yaw key ") + code, true, std::move(bytes)});
    }
    // coupled_aim: mode 0 is dropped, any other mode is carried.
    for (const char* mode : {"0", "5"}) {
        inputs.push_back({std::string("inject mode ") + mode, true,
                          FirstRunFile() + "[Diag]\r\nInjectMode=" + mode + "\r\n"});
    }
    for (testing::IniMutation& m : testing::GenerateIniMutations(FirstRunFile(), CorpusReads(), CorpusKeys())) {
        inputs.push_back({"corpus: " + m.name, true, std::move(m.bytes)});
    }
    return inputs;
}

// ---- Checks --------------------------------------------------------------------

// The first-run file committed as test data is what the published build writes.
void FirstRunFileIsThePublishedBuilds() {
    Scratch s;
    tow_oracle::WriteDefaultIfMissing(s.dir());
    CHECK_MSG(ReadFileBytes(s.ini()) == FirstRunFile(),
              "v0.1.0-first-run.ini is what the published build writes at first run");
}

// Comparison 1. What the published build did that the import does not, each
// with the commit that changed it:
//
// - c7a9c9b (feat: shooter ADS handling - head tracking stays on through the
//   sights) removed the ADS mode cycle. The published build read [Aim] AdsMode
//   (paused or tracked) as the mode it started in, and cycled it on Insert and
//   Ctrl+Shift+U. The import reads no AdsMode: head tracking stays on through
//   the aim, and Insert and Ctrl+Shift+U do nothing.
//
// Nothing else may differ, floats bit for bit.
void OracleAgainstImport(const std::vector<Input>& inputs) {
    int compared = 0;
    for (const Input& input : inputs) {
        Scratch s;
        if (input.present) s.Write(input.bytes);

        const OracleReading oracle = ReadOracle(s.dir());
        tow_ht::legacy::Config imported;
        tow_ht::legacy::Load(s.dir(), imported);
        const Observed import = ObserveLegacy(imported);

        Observed published = oracle.observed;
        published.hotkeys.erase(std::remove_if(published.hotkeys.begin(), published.hotkeys.end(),
                                               [](const Hotkey& h) { return std::get<0>(h) == kAdsMode; }),
                                published.hotkeys.end());
        CHECK_MSG(oracle.ads_mode == 0 || oracle.ads_mode == 2, "the published build started paused or tracked");

        const std::vector<std::string> diff = Differences(published, import);
        for (const std::string& d : diff) std::printf("  comparison 1, %s: %s\n", input.name.c_str(), d.c_str());
        CHECK_MSG(diff.empty(), "comparison 1: oracle and import agree apart from c7a9c9b's ADS mode");
        const bool same_pose = SamePoseShaping(oracle.pose, LegacyPoseShapingOf(imported));
        if (!same_pose) std::printf("  comparison 1, %s: pose shaping\n", input.name.c_str());
        CHECK_MSG(same_pose, "comparison 1: oracle and import read the same pose shaping");
        ++compared;
    }
    std::printf("comparison 1: %d inputs\n", compared);
}

Observed ObserveCanonical(const tow_ht::Config& c) {
    Observed o;
    o.udp_port = c.udp_port;
    o.start_enabled = c.enable_on_startup;
    o.start_world_yaw = c.world_space_yaw;
    o.start_mode = static_cast<int>(cameraunlock::DecodeTrackingMode(c.rotation_enabled, c.position_enabled).value());
    o.center_window = c.center_window;
    o.local_smoothing = c.local_smoothing;
    o.remote_smoothing = c.remote_smoothing;
    const float limits[5] = {c.limit_x, c.limit_y, c.limit_y_down, c.limit_z, c.limit_z_back};
    std::copy(std::begin(limits), std::end(limits), o.limits);
    // view_hook.cpp moves the crosshair on every frame the pose applies, over
    // ReticleMover's widget list.
    o.crosshair_follows = true;
    o.crosshair_widgets = tow_ht::kCrosshairWidgets;
    o.aim_trace_channel = c.aim_trace_channel;
    o.aim_trace_distance = c.aim_trace_distance;
    o.collision_enabled = c.collision_enabled;
    o.collision_margin = c.collision_margin;
    o.collision_channel = c.collision_channel;
    o.collision_release_smoothing = c.collision_release_smoothing;
    o.widget_dump = c.widget_dump;
    o.widget_dump_outer = c.widget_dump_outer;
    o.pose_log = c.pose_log;
    o.inject_mode = c.inject_mode;
    // mod_hotkeys.cpp Register: each list through ParseKeyBindings and
    // RegisterKeyBindings.
    const std::pair<Action, const std::string*> lists[] = {{kToggle, &c.toggle_key},
                                                           {kCycleMode, &c.cycle_tracking_mode_key},
                                                           {kYawMode, &c.yaw_mode_key},
                                                           {kCycleInject, &c.inject_mode_key}};
    for (const auto& [action, list] : lists) {
        const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(*list);
        CHECK_MSG(parsed.ok(), "a migrated key list parses");
        for (const cameraunlock::input::KeyBinding& b : parsed.bindings) {
            o.hotkeys.push_back({action, b.vk, static_cast<unsigned>(b.modifiers)});
        }
    }
    std::sort(o.hotkeys.begin(), o.hotkeys.end());
    return o;
}

// What the migration must run on for what the import read: the import's
// reading, less the approved changes. Each change is also a dropped value, in
// the map's order, which `drops` collects.
Observed ExpectedMigration(const tow_ht::legacy::Config& read, std::vector<cfg::DroppedValue>& drops) {
    Observed o = ObserveLegacy(read);
    if (!o.collision_enabled) {
        drops.push_back({cfg::DropRule::FollowsDefault, "Collision", "Enabled", "false"});
        o.collision_enabled = true;
    }
    if (read.inject_mode == 0) {
        drops.push_back({cfg::DropRule::CoupledAim, "Diag", "InjectMode", "0"});
        o.inject_mode = -1;
    }
    // N3: a yaw key on Ctrl, Shift or Alt alone is unbound, and the chord stays.
    const int yaw = read.yaw_mode_key;
    if ((yaw >= 0x10 && yaw <= 0x12) || (yaw >= 0xA0 && yaw <= 0xA5)) {
        char code[8];
        std::snprintf(code, sizeof(code), "0x%X", yaw);
        drops.push_back({cfg::DropRule::ModifierKey, "Hotkeys", "YawModeKey", code});
        o.hotkeys.erase(std::find(o.hotkeys.begin(), o.hotkeys.end(), Hotkey{kYawMode, yaw, kPlain}));
    }
    if (!o.crosshair_follows) {
        drops.push_back({cfg::DropRule::Reticle, "Reticle", "Enabled", "false"});
        o.crosshair_follows = true;
    }
    // A widget list at the shipped value is not a drop, so the mod's own list has
    // to be that value.
    if (o.crosshair_widgets != tow_ht::legacy::kDefaultReticleTargets) {
        drops.push_back({cfg::DropRule::Reticle, "Reticle", "Targets", o.crosshair_widgets});
        o.crosshair_widgets = tow_ht::legacy::kDefaultReticleTargets;
    }
    return o;
}

bool SameDrop(const cfg::DroppedValue& a, const cfg::DroppedValue& b) {
    return a.rule == b.rule && a.section == b.section && a.key == b.key && a.value == b.value;
}

// The import's dropped values are exactly `expected` and then the pose shaping
// set away from what shipped. The pose_shaping entries name one sensitivity or
// inversion each, in the frozen reader's order, folded exactly when the value is
// the shipped one; a value that is not folded is dropped as PoseShaping.
bool DropsAreTheApprovedOnes(const tow_ht::legacy::Config& read, const std::vector<cfg::DroppedValue>& expected,
                             const cfg::ImportResult& result) {
    if (result.dropped.size() < expected.size()) return false;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (!SameDrop(result.dropped[i], expected[i])) return false;
    }
    const tow_ht::legacy::Config shipped{};
    struct Expected {
        const char* section;
        const char* key;
        bool folded;
    };
    const Expected pose[] = {
        {"Rotation", "YawSensitivity", Bits(read.yaw_sensitivity) == Bits(shipped.yaw_sensitivity)},
        {"Rotation", "PitchSensitivity", Bits(read.pitch_sensitivity) == Bits(shipped.pitch_sensitivity)},
        {"Rotation", "RollSensitivity", Bits(read.roll_sensitivity) == Bits(shipped.roll_sensitivity)},
        {"Rotation", "InvertYaw", read.invert_yaw == shipped.invert_yaw},
        {"Rotation", "InvertPitch", read.invert_pitch == shipped.invert_pitch},
        {"Rotation", "InvertRoll", read.invert_roll == shipped.invert_roll},
        {"Position", "SensitivityX", Bits(read.position_sensitivity_x) == Bits(shipped.position_sensitivity_x)},
        {"Position", "SensitivityY", Bits(read.position_sensitivity_y) == Bits(shipped.position_sensitivity_y)},
        {"Position", "SensitivityZ", Bits(read.position_sensitivity_z) == Bits(shipped.position_sensitivity_z)},
    };
    if (result.pose_shaping.size() != std::size(pose)) return false;
    std::size_t dropped = expected.size();
    for (std::size_t i = 0; i < std::size(pose); ++i) {
        const cfg::PoseShapingValue& got = result.pose_shaping[i];
        if (got.section != pose[i].section || got.key != pose[i].key) return false;
        if (got.folded != pose[i].folded) return false;
        if (got.folded) continue;
        if (dropped >= result.dropped.size()) return false;
        const cfg::DroppedValue& d = result.dropped[dropped++];
        if (d.rule != cfg::DropRule::PoseShaping || d.section != got.section || d.key != got.key ||
            d.value != got.value) {
            return false;
        }
    }
    return dropped == result.dropped.size();
}

// CRLF line ends and no control byte. A byte above 0x7F is allowed here: a string
// row carries the player's bytes as they are, which the corpus's cp1252 case puts
// in [Dev] WidgetDumpOuter. lint-migrated.mjs holds that allowance to that row.
bool Crlf(const std::string& bytes) {
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(bytes[i]);
        if (c == 0x7F) return false;
        if (c == '\r' && (i + 1 == bytes.size() || bytes[i + 1] != '\n')) return false;
        if (c == '\n' && (i == 0 || bytes[i - 1] != '\r')) return false;
        if (c < 0x20 && c != '\r' && c != '\n') return false;
    }
    return !bytes.empty() && bytes.back() == '\n';
}

// A Defaults.ini that differs from the built-in values on every global row the
// table binds, as a player who set their own everywhere would have it.
constexpr const char* kPlayerDefaults =
    "[Network]\r\nUdpPort=5151\r\n"
    "[General]\r\nEnableOnStartup=false\r\nWorldSpaceYaw=false\r\nRotationEnabled=false\r\n"
    "[Smoothing]\r\nLocalSmoothing=0.25\r\nRemoteSmoothing=0.5\r\n"
    "[Position]\r\nPositionEnabled=true\r\nPositionLimitX=0.5\r\nPositionLimitY=0.3\r\nPositionLimitYDown=0.1\r\n"
    "PositionLimitZ=0.6\r\nPositionLimitZBack=0.2\r\nCollisionEnabled=false\r\nCollisionReleaseSmoothing=0.5\r\n"
    "[Hotkeys]\r\nToggleKey=F8\r\nCycleTrackingModeKey=F9\r\nYawModeKey=F10\r\n";

// What the session runs on over kPlayerDefaults: `o`, with each row the import
// left to Defaults.ini as that file gives it.
Observed OverPlayerDefaults(Observed o, const std::set<Concept>& follows) {
    const auto follows_row = [&](Concept row) { return follows.count(row) != 0; };
    if (follows_row(Concept::UdpPort)) o.udp_port = 5151;
    if (follows_row(Concept::EnableOnStartup)) o.start_enabled = false;
    if (follows_row(Concept::WorldSpaceYaw)) o.start_world_yaw = false;
    if (follows_row(Concept::RotationEnabled)) o.start_mode = static_cast<int>(TrackingMode::PositionOnly);
    if (follows_row(Concept::LocalSmoothing)) o.local_smoothing = 0.25f;
    if (follows_row(Concept::RemoteSmoothing)) o.remote_smoothing = 0.5f;
    const std::pair<Concept, float> limits[5] = {{Concept::PositionLimitX, 0.5f}, {Concept::PositionLimitY, 0.3f},
                                                 {Concept::PositionLimitYDown, 0.1f}, {Concept::PositionLimitZ, 0.6f},
                                                 {Concept::PositionLimitZBack, 0.2f}};
    for (int i = 0; i < 5; ++i) {
        if (follows_row(limits[i].first)) o.limits[i] = limits[i].second;
    }
    if (follows_row(Concept::CollisionEnabled)) o.collision_enabled = false;
    if (follows_row(Concept::CollisionReleaseSmoothing)) o.collision_release_smoothing = 0.5f;
    // F8, F9 and F10.
    const std::tuple<Concept, int, int> keys[] = {{Concept::ToggleKey, kToggle, 0x77},
                                                  {Concept::CycleTrackingModeKey, kCycleMode, 0x78},
                                                  {Concept::YawModeKey, kYawMode, 0x79}};
    for (const auto& [row, action, vk] : keys) {
        if (!follows_row(row)) continue;
        const int a = action;
        o.hotkeys.erase(std::remove_if(o.hotkeys.begin(), o.hotkeys.end(),
                                       [a](const Hotkey& h) { return std::get<0>(h) == a; }),
                        o.hotkeys.end());
        o.hotkeys.push_back({action, vk, kPlain});
    }
    std::sort(o.hotkeys.begin(), o.hotkeys.end());
    return o;
}

// How one migration of an input is set up.
enum class Setup { kBuiltInDefaults, kReadOnlyLegacy, kPlayerDefaults };

const char* SetupName(Setup setup) {
    switch (setup) {
        case Setup::kBuiltInDefaults: return "built-in Defaults.ini";
        case Setup::kReadOnlyLegacy: return "read-only HeadTracking.ini";
        case Setup::kPlayerDefaults: return "player's Defaults.ini";
    }
    throw std::logic_error("unknown setup");
}

// One migration of `input` under `setup`, held to `expected` where the input is
// a legacy file. Returns the CameraUnlock.ini it wrote.
std::string Migrate(const Input& input, Setup setup, const Observed& expected) {
    const std::string name = input.name + ", " + SetupName(setup);
    Scratch s;
    if (setup == Setup::kPlayerDefaults) s.WriteDefaults(kPlayerDefaults);
    if (input.present) {
        s.Write(input.bytes);
        if (setup == Setup::kReadOnlyLegacy) SetFileAttributesA(s.ini().c_str(), FILE_ATTRIBUTE_READONLY);
    }
    const FileState legacy = StateOf(s.ini());

    cfg::ConfigOwner<tow_ht::Config> owner(tow_ht::config::OwnerOptions(s.wdir(), s.defaults()));
    const cfg::ConfigLoadResult<tow_ht::Config> loaded = owner.Load();
    const cfg::ConfigLoadStatus want = input.present ? cfg::ConfigLoadStatus::Migrated : cfg::ConfigLoadStatus::Created;
    if (loaded.status != want) {
        std::printf("  comparison 2, %s: %s, %s\n", name.c_str(), cfg::ConfigLoadStatusName(loaded.status),
                    loaded.reason.c_str());
    }
    CHECK_MSG(loaded.status == want, "every legacy input is imported, and no file is created");
    CHECK_MSG(StateOf(s.ini()) == legacy, "HeadTracking.ini keeps its bytes, write time and attributes");
    const std::vector<std::string> names =
        input.present ? std::vector<std::string>{"CameraUnlock.ini", "HeadTracking.ini"}
                      : std::vector<std::string>{"CameraUnlock.ini"};
    CHECK_MSG(s.Names() == names, "the folder holds HeadTracking.ini and CameraUnlock.ini and nothing else");

    // With no legacy file the owner creates the fresh file, which follows
    // Defaults.ini, so only an import is held to what the import read.
    if (input.present || setup == Setup::kBuiltInDefaults) {
        const std::vector<std::string> diff = Differences(expected, ObserveCanonical(loaded.config));
        for (const std::string& d : diff) std::printf("  comparison 2, %s: %s\n", name.c_str(), d.c_str());
        CHECK_MSG(diff.empty(), "comparison 2: the migration runs as the import read, less the approved changes");
    }
    for (const cfg::CanonicalDiagnostic& d : loaded.diagnostics) {
        std::printf("  %s: migrated file, %s\n", name.c_str(), cfg::DescribeCanonicalDiagnostic(d).c_str());
    }
    CHECK_MSG(loaded.diagnostics.empty(), "the migrated file reads with no diagnostic");

    const std::string migrated = ReadFileBytes(s.canonical());
    CHECK_MSG(Crlf(migrated), "the migrated file has CRLF line ends and no control byte");

    // The next start: over the same Defaults.ini it reads CameraUnlock.ini, does
    // not import, gives the same settings, and changes neither file.
    const FileState canonical = StateOf(s.canonical());
    const FileState defaults = StateOf(s.defaults_ini());
    cfg::ConfigOwner<tow_ht::Config> again(tow_ht::config::OwnerOptions(s.wdir(), s.defaults()));
    const cfg::ConfigLoadResult<tow_ht::Config> reloaded = again.Load();
    CHECK_MSG(reloaded.status == cfg::ConfigLoadStatus::Canonical, "the next start reads CameraUnlock.ini");
    CHECK_MSG(Differences(ObserveCanonical(reloaded.config), ObserveCanonical(loaded.config)).empty(),
              "the next start runs on the settings the migration gave");
    CHECK_MSG(StateOf(s.canonical()) == canonical, "the next start leaves CameraUnlock.ini as it was");
    CHECK_MSG(StateOf(s.ini()) == legacy, "the next start leaves HeadTracking.ini as it was");
    CHECK_MSG(StateOf(s.defaults_ini()) == defaults, "the next start leaves Defaults.ini as it was");
    CHECK_MSG(s.Names() == names, "the next start writes no other file");
    return migrated;
}

// Comparison 2, and what the conversion must do with every input besides.
void ImportAgainstMigration(const std::vector<Input>& inputs, std::set<std::string>& distinct) {
    const std::string committed = ReadFileBytes(std::string(TOW_SOURCE_DIR) + "/config/HeadTracking.ini");
    const cfg::ConfigTable<tow_ht::Config> table = tow_ht::config::Table();
    int compared = 0;
    int touched = 0;
    int mode_touched = 0;
    int coupled = 0;
    for (const Input& input : inputs) {
        const char* name = input.name.c_str();

        // The import, run as the owner runs it but on a read-only copy: it reads
        // what the frozen reader reads, records the drops, and writes nothing.
        cfg::ImportResult imported;
        tow_ht::legacy::Config read;
        {
            Scratch ro;
            if (input.present) {
                ro.Write(input.bytes);
                SetFileAttributesA(ro.ini().c_str(), FILE_ATTRIBUTE_READONLY);
            }
            const FileState before = StateOf(ro.ini());
            tow_ht::Config unused = table.defaults();
            imported = tow_ht::legacy::Import().run({ro.wini(), ro.ini(), false}, unused);
            CHECK_MSG(StateOf(ro.ini()) == before, "the import leaves the legacy file as it was");
            CHECK_MSG(ro.Names() == (input.present ? std::vector<std::string>{"HeadTracking.ini"}
                                                   : std::vector<std::string>{}),
                      "the import writes no file");
            tow_ht::legacy::Load(ro.dir(), read);
        }
        CHECK_MSG(imported.status == (input.present ? cfg::ImportStatus::Imported : cfg::ImportStatus::Absent),
                  "the import reads every input, as the published build did");
        std::vector<cfg::DroppedValue> approved;
        const Observed expected = ExpectedMigration(read, approved);
        const bool drops_ok = DropsAreTheApprovedOnes(read, approved, imported);
        if (!drops_ok) std::printf("  comparison 2, %s: dropped values\n", name);
        CHECK_MSG(drops_ok, "comparison 2: the only drops are the approved changes");
        if (read.inject_mode == 0) ++coupled;

        const std::set<Concept> follows(imported.follows_defaults_ini.begin(), imported.follows_defaults_ini.end());
        CHECK_MSG(follows.size() == imported.follows_defaults_ini.size(), "follows_defaults_ini names each row once");
        const std::set<Concept> untouched = UntouchedRows(ObserveLegacy(read));
        if (follows != untouched) {
            std::printf("  %s: follows Defaults.ini %s, untouched %s\n", name, Names(follows).c_str(),
                        Names(untouched).c_str());
        }
        CHECK_MSG(follows == untouched, "the rows left to Defaults.ini are exactly the ones the player never changed");
        if (untouched != AllRows()) ++touched;
        if (!untouched.count(Concept::RotationEnabled)) ++mode_touched;
        const bool unedited =
            input.name == "v0.1.0 first-run file" || input.name == "no file" || input.name == "empty file";
        if (unedited) CHECK_MSG(untouched == AllRows(), "an unedited input leaves every row to Defaults.ini");

        const std::string migrated = Migrate(input, Setup::kBuiltInDefaults, expected);
        distinct.insert(migrated);
        CHECK_MSG(Migrate(input, Setup::kReadOnlyLegacy, expected) == migrated,
                  "a read-only HeadTracking.ini is imported as a writable one is");
        const std::string over_player =
            Migrate(input, Setup::kPlayerDefaults, OverPlayerDefaults(expected, follows));
        distinct.insert(over_player);
        if (input.present) {
            for (const Concept row : follows) {
                const std::string key = cfg::schema::kConcepts[static_cast<std::size_t>(row)].key;
                const bool is_default = over_player.find("\r\n" + key + "=default\r\n") != std::string::npos;
                if (!is_default) std::printf("  %s, player's Defaults.ini: %s is not default\n", name, key.c_str());
                CHECK_MSG(is_default, "a row the player never changed is written default");
            }
        }

        // Fresh equals upgrade: over the built-in Defaults.ini, the published
        // build's first-run file, an empty file and no file at all end as the
        // committed file, `default` on every global row.
        if (unedited) {
            CHECK_MSG(migrated == committed, "the first-run file, an empty file and no file give the committed file");
        }
        ++compared;
    }
    std::printf("comparison 2: %d inputs, %zu distinct files\n", compared, distinct.size());
    std::printf("%d inputs changed a row from the shipped default, %d of them the tracking mode\n", touched,
                mode_touched);
    CHECK_MSG(touched > 0 && mode_touched > 0, "the corpus changes rows, the tracking mode among them");
    std::printf("%d inputs held [Diag] InjectMode=0, dropped as CoupledAim\n", coupled);
    CHECK_MSG(coupled > 0, "the inputs hold the inject mode that coupled the aim");
}

void WriteForLint(const std::set<std::string>& distinct) {
    char exe[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (length == 0 || length == MAX_PATH) throw std::runtime_error("cannot read the test's own path");
    std::string dir(exe, length);
    dir = dir.substr(0, dir.find_last_of('\\')) + "\\migrated";
    if (!CreateDirectoryA(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        throw std::runtime_error("cannot create " + dir + ", error " + std::to_string(GetLastError()));
    }
    WIN32_FIND_DATAA found;
    const HANDLE h = FindFirstFileA((dir + "\\*.ini").c_str(), &found);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::string stale = dir + "\\" + found.cFileName;
            if (!DeleteFileA(stale.c_str())) throw std::runtime_error("cannot delete " + stale);
        } while (FindNextFileA(h, &found));
        FindClose(h);
    }
    int n = 0;
    for (const std::string& bytes : distinct) {
        const std::string path = dir + "\\" + std::to_string(n++) + ".ini";
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) throw std::runtime_error("cannot write " + path);
    }
}

}  // namespace

int main() {
    SourcesAreThePinnedOnes();
    FirstRunFileIsThePublishedBuilds();
    const std::vector<Input> inputs = Inputs();
    OracleAgainstImport(inputs);
    std::set<std::string> distinct;
    ImportAgainstMigration(inputs, distinct);
    WriteForLint(distinct);
    return tow_test::Report();
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include <cmath>
#include <cstdio>
#include <windows.h>

#include "cameraunlock/config/ini_reader.h"
#include "config_sanitize.h"
#include "inject_mode.h"
#include "logging.h"

namespace tow_ht {

namespace {

constexpr const char* kIniName = "HeadTracking.ini";

// Held from config_load so config_save_ads_mode can write back to the same file
// the player edited, rather than guessing at the game directory a second time.
std::string g_iniPath;

// The crosshair widgets, as read off a running game by the widget probe. Kept
// here rather than in the Config default so the shipped ini and the fallback
// are the same string.
//
// The tree, from the probe: HUD_BP_C / Reticle (Reticle_BP_C) / RootCanvas
// holds six children, and every one of them is drawn centred on the aim point,
// so all six move together:
//   Crosshair          GenericCrosshair_BP_C - the cross itself
//   ReticuleInteract   the look-at highlight
//   CauseDamageWidget  the hit marker
//   StealthOverlay     the pair of brackets either side of the cross
//   TTDOverlay         the Tactical Time Dilation ring
//   TTDDTOverlay       the Tactical Time Dilation readout
// Leaving one out leaves it parked at screen centre while the cross moves off
// to the shot, which is how StealthOverlay and TTDOverlay were found missing.
//
// Reticle itself is NOT in the list, and that is the finding: translating it
// moves nothing on screen, because its own Blueprint writes RenderTransform
// every tick and overwrites the push. Its children are left alone by that, and
// moving them works - measured, a commanded 177.1 px landed as 177 px.
//
// The outer runs from Reticle up to the game instance. The game instance is
// needed because the live widget tree and the Blueprint template it was built
// from share every link up to HUD_BP_C; only above that do they part company.
// Reticle is needed because CharacterOverview, the health bar, has a TTDOverlay
// of its own under the same game instance.
const char* kDefaultReticleTargets =
    "Crosshair@Reticle/WidgetTree/HUD_BP_C/IndianaGameInstance,"
    "ReticuleInteract@Reticle/WidgetTree/HUD_BP_C/IndianaGameInstance,"
    "CauseDamageWidget@Reticle/WidgetTree/HUD_BP_C/IndianaGameInstance,"
    "StealthOverlay@Reticle/WidgetTree/HUD_BP_C/IndianaGameInstance,"
    "TTDOverlay@Reticle/WidgetTree/HUD_BP_C/IndianaGameInstance,"
    "TTDDTOverlay@Reticle/WidgetTree/HUD_BP_C/IndianaGameInstance";

// Per-key fallbacks for a rejected smoothing value, matching the documented
// defaults in config.h. They differ on purpose: a malformed RemoteSmoothing must
// not drop back to the LOCAL default, which would leave a phone on WiFi running
// with no smoothing at all on raw network jitter.
const float kLocalSmoothingFallback =
    static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
const float kRemoteSmoothingFallback =
    static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

// What each key is allowed to hold. The fleet-wide ones (sensitivities,
// smoothing, the position limits) are the ranges AGENTS.md's configuration
// table publishes, so the ini and the documentation cannot drift apart. The
// three that table calls game-specific carry a range wide enough to admit any
// value a player would actually set and narrow enough that a typo is caught:
//
//  - the two trace channels are written into a Kismet parameter frame as a
//    single byte, so 0-255 is not a policy but the width of the slot. Without
//    the check a channel of 300 became channel 44, silently, and the sweep then
//    blocked on nothing or on everything with the log reporting the 300.
//  - the aim cast's distance is centimetres, so this spans 1cm to 10km.
//  - the lean standoff is centimetres too, and has to be positive to be a
//    standoff at all.
constexpr float kMinSensitivity = 0.1f;
constexpr float kMaxSensitivity = 3.0f;
constexpr float kMinPositionSensitivity = 0.0f;
constexpr float kMaxPositionSensitivity = 5.0f;
constexpr float kMinPositionLimit = 0.01f;
constexpr float kMaxPositionLimit = 0.5f;
constexpr float kMinSmoothing = 0.0f;
constexpr float kMaxSmoothing = 1.0f;
constexpr float kMinAimDistance = 1.0f;
constexpr float kMaxAimDistance = 1000000.0f;
// The standoff has to EXCEED the near clip distance or the rule buys nothing:
// geometry closer to the eye than the near plane is culled, so a wall held at
// 2cm with a 10cm near plane is still not drawn and the player still sees
// through it - the same complaint the sweep exists to remove, with the sweep
// switched on. 10.0 is UE 4.27's own GNearClippingPlane default in centimetres;
// this game's live value has not been read out of the running process, so a
// title that raises its near plane above the shipped 12.0 default would still
// need that number pinned in the build profile.
constexpr float kNearClipFloorCm = 10.0f;
constexpr float kMinCollisionRadius = kNearClipFloorCm + 1.0f;
constexpr float kMaxCollisionRadius = 200.0f;

// The documented UdpPort range. Below 1024 needs privileges the game does not
// have, and the whole range has to fit the uint16_t the socket is opened with -
// without the check a port of 70000 was truncated to 4464 and nothing said so.
constexpr int kMinUdpPort = 1024;
constexpr int kMaxUdpPort = 65535;

// TraceTypeQuery / ECollisionChannel, as they are written into the frame.
constexpr int kMinTraceChannel = 0;
constexpr int kMaxTraceChannel = 255;

// Virtual-key codes. 0 is "no key" and 0xFF is the OEM catch-all, so neither is
// bindable; a value outside the range is not a key code at all.
constexpr int kMinVirtualKey = 0x01;
constexpr int kMaxVirtualKey = 0xFE;

// The nav-cluster keys mod_hotkeys.cpp registers unconditionally. They are fixed
// across the fleet so the same action sits on the same key in every mod, which
// makes them the one thing YawModeKey must not be: the poller dispatches to
// EVERY handler registered on a code, so a YawModeKey of 0x23 makes one press of
// End toggle tracking AND flip the yaw mode, and the second action is invisible
// until the view starts leaning on pitched turns for no reason the player can
// see. Nothing else in the process compares the two.
constexpr int kFixedNavKeys[] = {
    0x23,  // End    - toggle tracking
    0x21,  // PageUp - cycle tracking mode
    0x2D,  // Insert - cycle ADS mode
};

const char* FixedNavKeyName(int vk) {
    switch (vk) {
        case 0x23: return "End (toggle tracking)";
        case 0x21: return "Page Up (cycle tracking mode)";
        case 0x2D: return "Insert (cycle ADS mode)";
        default:   return "another binding";
    }
}

std::string ini_path(const std::string& exe_dir) {
    return exe_dir + "\\" + kIniName;
}

// Every float the ini carries, through the one boundary check, with the key
// named in whatever line comes out. config_sanitize.h carries why the check
// exists and why a range test alone does not do it.
//
// Validation, never a floor: a finite value inside the range is returned
// untouched, so a deliberately configured 0.0 stays 0.0.
float sanitize_float(const char* section, const char* key, float v,
                            float fallback, float lo, float hi) {
    const float safe = config_sanitize::Float(v, fallback, lo, hi);
    // Not `safe != v`: that comparison is false for a NaN v, which is the case
    // this exists to report.
    if (safe == v) return v;
    if (!std::isfinite(v)) {
        Log::Line("config: [%s] %s is not a finite number, using %.3f", section, key, safe);
    } else {
        Log::Line("config: [%s] %s %.3f is outside %.3f-%.3f, using %.3f",
                  section, key, v, lo, hi, safe);
    }
    return safe;
}

// One float key, read as text and parsed here rather than through
// IniReader::ReadFloat, which is a PREFIX parse: it returns whatever the leading
// characters came to and says nothing about the rest. A value whose prefix lands
// inside the documented range is then accepted in silence, because sanitize_float
// only speaks when it changes something. `RemoteSmoothing=0,15` is the case that
// bites - a decimal comma parses as 0.0, 0.0 is a legal smoothing value, and the
// player has silently lost all smoothing on a WiFi tracker.
//
// `fallback` is the value used when the key is absent, when it does not parse,
// and when a non-finite value lands on it.
float read_float(const cameraunlock::IniReader& ini, const char* section,
                        const char* key, float fallback, float lo, float hi) {
    const std::string raw = ini.ReadString(section, key, "");
    if (raw.find_first_not_of(" \t\r\n") == std::string::npos) return fallback;
    float parsed = fallback;
    if (!config_sanitize::ParseFloat(raw, parsed)) {
        Log::Line("config: [%s] %s is not a number, using %.3f", section, key, fallback);
        return fallback;
    }
    return sanitize_float(section, key, parsed, fallback, lo, hi);
}

// One boolean key, read and validated in one step. The parse itself lives in
// config_sanitize.h beside ParseInt, so it can be exercised without a file.
bool read_bool(const cameraunlock::IniReader& ini, const char* section,
                      const char* key, bool fallback) {
    const std::string raw = ini.ReadString(section, key, "");
    if (raw.find_first_not_of(" \t\r\n") == std::string::npos) return fallback;
    bool parsed = fallback;
    if (config_sanitize::ParseBool(raw, parsed)) return parsed;
    Log::Line("config: [%s] %s is not a yes/no value, using %d", section, key,
              fallback ? 1 : 0);
    return fallback;
}

// One integer key, read as text and parsed here rather than through
// IniReader::ReadInt, which answers 0 instead of the default for a
// present-but-unparseable value (ini_reader.h, rule 4). Out of range falls back
// rather than clamping - see config_sanitize::Int.
int sanitize_int(const cameraunlock::IniReader& ini, const char* section,
                        const char* key, int fallback, int lo, int hi) {
    int parsed = fallback;
    const std::string raw = ini.ReadString(section, key, "");
    if (!raw.empty() && !config_sanitize::ParseInt(raw, parsed)) {
        Log::Line("config: [%s] %s is not a whole number, using %d", section, key, fallback);
        return fallback;
    }
    const int safe = config_sanitize::Int(parsed, fallback, lo, hi);
    if (safe != parsed)
        Log::Line("config: [%s] %s %d is outside %d-%d, using %d",
                  section, key, parsed, lo, hi, safe);
    return safe;
}

// The old value is deliberately NOT migrated into the new keys. The single
// Smoothing value carried a hidden 0.15 floor, so the number in an existing
// config does not mean what it used to: copying it across would hand a local
// user smoothing they never chose under the new semantics, and copying it into
// only one of the two keys would be a guess about which connection they were on.
void WarnRetiredSmoothingKey(const cameraunlock::IniReader& reader,
                             const char* section, const char* key) {
    if (reader.ReadString(section, key, "").empty()) return;
    Log::Line(
        "WARNING: Config key [%s] %s has been retired and is IGNORED. Smoothing is "
        "now two keys: LocalSmoothing (default 0, applies to a tracker on this "
        "machine) and RemoteSmoothing (default 0.15, applies to a tracker on the "
        "network). The old value is not migrated because the semantics changed - it "
        "carried a hidden 0.15 floor that no longer exists. Set the two new keys.",
        section, key);
}

// One section each, in the order the file lays them out. The keys are a flat
// list and the sections are the only seam in it, so this is where it divides:
// each function reads exactly the section it is named for, and config_load
// stays the order they are asked in.
void ReadNetworkAndGeneral(const cameraunlock::IniReader& ini, Config& out) {
    out.udp_port           = sanitize_int(ini, "Network", "UdpPort", out.udp_port,
                                          kMinUdpPort, kMaxUdpPort);
    out.enable_on_startup  = read_bool(ini, "General",  "EnableOnStartup",  out.enable_on_startup);
    out.world_space_yaw    = read_bool(ini, "General",  "WorldSpaceYaw",    out.world_space_yaw);
    out.center_window      = read_bool(ini, "General",  "CenterWindow",     out.center_window);
}

void ReadRotation(const cameraunlock::IniReader& ini, Config& out) {
    out.yaw_sensitivity    = read_float(ini, "Rotation", "YawSensitivity",
        out.yaw_sensitivity, kMinSensitivity, kMaxSensitivity);
    out.pitch_sensitivity  = read_float(ini, "Rotation", "PitchSensitivity",
        out.pitch_sensitivity, kMinSensitivity, kMaxSensitivity);
    out.roll_sensitivity   = read_float(ini, "Rotation", "RollSensitivity",
        out.roll_sensitivity, kMinSensitivity, kMaxSensitivity);
    out.invert_yaw         = read_bool(ini, "Rotation", "InvertYaw",        out.invert_yaw);
    out.invert_pitch       = read_bool(ini, "Rotation", "InvertPitch",      out.invert_pitch);
    out.invert_roll        = read_bool(ini, "Rotation", "InvertRoll",       out.invert_roll);

    out.local_smoothing    = read_float(ini, "Rotation", "LocalSmoothing",
        kLocalSmoothingFallback, kMinSmoothing, kMaxSmoothing);
    out.remote_smoothing   = read_float(ini, "Rotation", "RemoteSmoothing",
        kRemoteSmoothingFallback, kMinSmoothing, kMaxSmoothing);

    WarnRetiredSmoothingKey(ini, "Rotation", "Smoothing");
}

void ReadPosition(const cameraunlock::IniReader& ini, Config& out) {
    out.position_enabled   = read_bool(ini, "Position", "Enabled",          out.position_enabled);
    out.position_sensitivity_x = read_float(ini, "Position", "SensitivityX",
        out.position_sensitivity_x, kMinPositionSensitivity, kMaxPositionSensitivity);
    out.position_sensitivity_y = read_float(ini, "Position", "SensitivityY",
        out.position_sensitivity_y, kMinPositionSensitivity, kMaxPositionSensitivity);
    out.position_sensitivity_z = read_float(ini, "Position", "SensitivityZ",
        out.position_sensitivity_z, kMinPositionSensitivity, kMaxPositionSensitivity);
    out.limit_x            = read_float(ini, "Position", "LimitX",
        out.limit_x, kMinPositionLimit, kMaxPositionLimit);
    out.limit_y            = read_float(ini, "Position", "LimitY",
        out.limit_y, kMinPositionLimit, kMaxPositionLimit);
    out.limit_z            = read_float(ini, "Position", "LimitZ",
        out.limit_z, kMinPositionLimit, kMaxPositionLimit);
    out.limit_z_back       = read_float(ini, "Position", "LimitZBack",
        out.limit_z_back, kMinPositionLimit, kMaxPositionLimit);
    // No position smoothing key: position uses the same LocalSmoothing /
    // RemoteSmoothing pair as rotation.
    WarnRetiredSmoothingKey(ini, "Position", "Smoothing");
}

void ReadReticle(const cameraunlock::IniReader& ini, Config& out) {
    out.reticle_enabled = read_bool(ini, "Reticle", "Enabled", out.reticle_enabled);
    out.reticle_targets = ini.ReadString("Reticle", "Targets", out.reticle_targets.c_str());
}

void ReadAim(const cameraunlock::IniReader& ini, Config& out) {
    out.aim_trace_channel  = sanitize_int(ini, "Aim", "TraceChannel",
        out.aim_trace_channel, kMinTraceChannel, kMaxTraceChannel);
    out.aim_trace_distance = read_float(ini, "Aim", "MaxDistance",
        out.aim_trace_distance, kMinAimDistance, kMaxAimDistance);

    // An absent key
    // is the default rather than an error, so a player upgrading from a release
    // written before the cycle existed keeps stock sights. A key that is present
    // and says something else is a typo, and it has to be named: the cycle key
    // writes the mode back on its first press, so an unreported typo is silently
    // overwritten by the value the player did not choose.
    // Parsed from the SAME token the check below judges, so the two cannot
    // disagree. ParseAdsMode on the raw string requires a whole-string match, so
    // `AdsMode=tracked ; keep tracking` silently fell back to paused while the
    // prefix check saw a valid name and said nothing - the value the player
    // typed discarded, with the one line written to catch that reporting success.
    // Going through the token gives AdsMode the same trailing-comment handling
    // the boolean keys already have.
    const std::string adsRaw = ini.ReadString("Aim", "AdsMode", "");
    const std::string adsToken = config_sanitize::LeadingToken(adsRaw);
    out.ads_mode = adsToken.empty() ? kDefaultAdsMode : ParseAdsMode(adsToken.c_str());
    if (!adsToken.empty() && adsToken != AdsModeValue(AdsMode::Paused) &&
        adsToken != AdsModeValue(AdsMode::Tracked)) {
        Log::Line("config: [Aim] AdsMode is not one of %s/%s, using %s",
                  AdsModeValue(AdsMode::Paused),
                  AdsModeValue(AdsMode::Tracked), AdsModeValue(out.ads_mode));
    }
}

void ReadCollision(const cameraunlock::IniReader& ini, Config& out) {
    out.collision_enabled = read_bool(ini, "Collision", "Enabled", out.collision_enabled);
    out.collision_radius  = read_float(ini, "Collision", "Radius",
        out.collision_radius, kMinCollisionRadius, kMaxCollisionRadius);
    out.collision_channel = sanitize_int(ini, "Collision", "Channel",
        out.collision_channel, kMinTraceChannel, kMaxTraceChannel);
    out.collision_release_smoothing = read_float(ini, "Collision", "ReleaseSmoothing",
        out.collision_release_smoothing, kMinSmoothing, kMaxSmoothing);
}

void ReadDev(const cameraunlock::IniReader& ini, Config& out) {
    out.widget_dump = read_bool(ini, "Dev", "WidgetDump", out.widget_dump);
    out.widget_dump_outer = ini.ReadString("Dev", "WidgetDumpOuter",
                                           out.widget_dump_outer.c_str());
    out.pose_log    = read_bool(ini, "Dev", "PoseLog",    out.pose_log);
    // -1 is "no override". Mode 0 hands EVERY GetPlayerViewPoint caller the head
    // pose, which is the aim decoupling switched off, so it must only ever be
    // reachable by asking for it - and IniReader::ReadInt answers 0 for a
    // present-but-unparseable value, which is why this goes through the strict
    // parse rather than that one.
    out.inject_mode = sanitize_int(ini, "Diag", "InjectMode", out.inject_mode,
                                   -1, inject::kModeCount - 1);
}

void ReadHotkeys(const cameraunlock::IniReader& ini, Config& out) {
    // ReadHex, not ReadInt: the key is written as 0x22 and
    // GetPrivateProfileIntA behind ReadInt stops at the x, which would bind the
    // toggle to virtual key 0 and lose Page Down with nothing in the log. The
    // range check catches the same outcome arriving by a different route - a
    // YawModeKey of 0, or of something too wide to be a key code, would take
    // the yaw toggle away just as quietly.
    const int yawKey = ini.ReadHex("Hotkeys", "YawModeKey", out.yaw_mode_key);
    out.yaw_mode_key = config_sanitize::Int(yawKey, out.yaw_mode_key,
                                            kMinVirtualKey, kMaxVirtualKey);
    if (out.yaw_mode_key != yawKey) {
        Log::Line("config: [Hotkeys] YawModeKey 0x%x is not a bindable virtual-key "
                  "code, using 0x%x", yawKey, out.yaw_mode_key);
        return;
    }
    // The compiled default rather than a second literal 0x22, so this and
    // config.h cannot drift; `out` is no use here, it already holds the value
    // being rejected.
    const int pageDown = Config{}.yaw_mode_key;
    for (const int taken : kFixedNavKeys) {
        if (out.yaw_mode_key != taken) continue;
        Log::Line("config: [Hotkeys] YawModeKey 0x%x is already %s, and one press "
                  "would fire both actions - using 0x%x (Page Down) instead",
                  taken, FixedNavKeyName(taken), pageDown);
        out.yaw_mode_key = pageDown;
        return;
    }
}

}  // namespace

void config_load(const std::string& exe_dir, Config& out) {
    out.reticle_targets = kDefaultReticleTargets;
    g_iniPath = ini_path(exe_dir);
    cameraunlock::IniReader ini;
    if (!ini.Open(g_iniPath)) {
        Log::Line("config: could not read %s - every setting below is the compiled "
                  "default, and edits to a file elsewhere will not be seen",
                  g_iniPath.c_str());
        return;
    }

    ReadNetworkAndGeneral(ini, out);
    ReadRotation(ini, out);
    ReadPosition(ini, out);
    ReadReticle(ini, out);
    ReadAim(ini, out);
    ReadCollision(ini, out);
    ReadDev(ini, out);
    ReadHotkeys(ini, out);
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
        "; cannot be a key this mod already uses - End (0x23), Page Up (0x21)\n"
        "; or Insert (0x2D) - because one press would then fire both actions.\n"
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
        "MaxDistance=20000\n"
        "; What head tracking does while the sights are up. Insert (or\n"
        "; Ctrl+Shift+U) cycles this in game and writes the new value back here.\n"
        ";   paused   - tracking stands down for the aim (default, stock sights)\n"
        ";   tracked  - tracking stays live, using the game's reticle\n"
        "AdsMode=%s\n\n"
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
        kDefaultReticleTargets, AdsModeValue(kDefaultAdsMode));
    std::fclose(f);
}

void config_save_ads_mode(AdsMode mode) {
    if (g_iniPath.empty()) return;
    if (!WritePrivateProfileStringA("Aim", "AdsMode", AdsModeValue(mode),
                                    g_iniPath.c_str())) {
        Log::Line("config: could not save AdsMode to %s (error %lu) - the setting "
                  "applies for this session but will not survive a restart",
                  g_iniPath.c_str(), GetLastError());
    }
}

}  // namespace tow_ht

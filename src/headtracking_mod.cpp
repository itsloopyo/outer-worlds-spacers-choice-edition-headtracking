// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "headtracking_mod.h"

#include <memory>
#include <string>

#include <windows.h>
#include <psapi.h>

#include "builds/build_registry.h"
#include "config.h"
#include "logging.h"
#include "mod_hotkeys.h"
#include "reticle_mover.h"
#include "session.h"
#include "view_hook.h"
#include "window_centering.h"

#include "cameraunlock/diagnostics/crash_handler.h"
#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/unreal/ue_runtime.h"

namespace tow_ht {

namespace {

namespace ue = ::cameraunlock::unreal;
using cameraunlock::TrackingMode;

// Whether GetModuleHandleExW's PIN succeeded. The no-teardown design in
// dllmain.cpp rests on it, so it is recorded and reported rather than assumed.
bool g_pinned = false;

Config g_config;
std::unique_ptr<cameraunlock::UdpReceiver> g_receiver;
std::unique_ptr<Session> g_session;

void ApplyConfigToSession() {
    cameraunlock::SensitivitySettings sens;
    sens.yaw          = g_config.yaw_sensitivity;
    sens.pitch        = g_config.pitch_sensitivity;
    sens.roll         = g_config.roll_sensitivity;
    sens.invert_yaw   = g_config.invert_yaw;
    sens.invert_pitch = g_config.invert_pitch;
    sens.invert_roll  = g_config.invert_roll;
    g_session->GetProcessor().SetSensitivity(sens);

    auto& ps = g_session->GetPositionProcessor().GetSettings();
    ps.sensitivity_x = g_config.position_sensitivity_x;
    ps.sensitivity_y = g_config.position_sensitivity_y;
    ps.sensitivity_z = g_config.position_sensitivity_z;
    ps.limit_x       = g_config.limit_x;
    // The clamp is [-limit_y_down, +limit_y] and limit_y_down carries its own
    // default, so mirror the one configured vertical limit the way
    // PositionSettings::Symmetric does. Left unset, raising LimitY widened the
    // upward budget only and downward travel stayed pinned at 0.20m.
    ps.limit_y       = g_config.limit_y;
    ps.limit_y_down  = g_config.limit_y;
    ps.limit_z       = g_config.limit_z;
    ps.limit_z_back  = g_config.limit_z_back;

    // Both smoothing values go into both processors; which one applies is
    // decided per frame from the packet source address inside Update().
    g_session->SetLocalSmoothing(g_config.local_smoothing);
    g_session->SetRemoteSmoothing(g_config.remote_smoothing);

    g_session->SetMode(g_config.position_enabled
        ? TrackingMode::RotationAndPosition
        : TrackingMode::RotationOnly);
}

// Both of these check GetModuleFileName's return value rather than the buffer
// alone, and that return carries two distinct failures. Zero means nothing was
// written, and the buffer would then be read as a C string over whatever the
// stack happened to hold - the log path and the ini path both come from here, so
// that is an unbounded read feeding a garbage path. MAX_PATH means the exe path
// did not fit and what came back is a TRUNCATED directory, which is not this
// game's directory and is where the ini would then be written.
//
// Either way the answer is the process working directory, which is what the
// no-separator branch already returned and what a game started through an ASI
// loader normally runs in.
std::wstring ExeDir() {
    wchar_t path[MAX_PATH] = {};
    const DWORD written = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (written == 0 || written >= MAX_PATH) return L".";
    std::wstring s(path, written);
    const auto slash = s.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : s.substr(0, slash);
}

// Narrow sibling of ExeDir for the ANSI IniReader (GetPrivateProfile*A).
std::string ExeDirNarrow() {
    char path[MAX_PATH] = {};
    const DWORD written = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (written == 0 || written >= MAX_PATH) return ".";
    std::string s(path, written);
    const auto slash = s.find_last_of("\\/");
    return slash == std::string::npos ? "." : s.substr(0, slash);
}

// Log::Open keeps the outgoing session as HeadTracking.prev.log and truncates
// the new one, so the file cannot grow across sessions. The crash handler's
// report lands in this same file, and the rotation is what stops the relaunch
// the player makes to reproduce a crash from erasing the report of it.
void OpenLog() {
    Log::Open(ExeDir() + L"\\HeadTracking.log");
}

// Why the mod is standing down, in the words the player's log needs.
//
// No default case: every enumerator is named, so adding one to MatchResult is a
// compiler warning here rather than a wrong diagnostic inherited silently.
void LogDormantReason(builds::MatchResult match) {
    switch (match) {
        case builds::MatchResult::HostNewer:
            Log::Line("build-check: this game build is NEWER than any profile this "
                      "mod knows about - check the releases page for an update. "
                      "Staying dormant; game runs vanilla.");
            break;
        case builds::MatchResult::HostOlder:
            Log::Line("build-check: this game build is OLDER than the profile - let "
                      "Steam finish updating. Staying dormant; game runs vanilla.");
            break;
        case builds::MatchResult::HostDiffers:
            Log::Line("build-check: this EXE carries a known build's timestamp with a "
                      "different size or checksum, so it has been repacked or "
                      "modified. This mod does not engage on a modified binary. "
                      "Staying dormant; game runs vanilla.");
            break;
        case builds::MatchResult::ProfileIncomplete:
            // SelectProfile already named the profile and what is missing.
            Log::Line("build-check: staying dormant; game runs vanilla.");
            break;
        case builds::MatchResult::ReadFailed:
            Log::Line("build-check: the host EXE's PE header could not be read, so "
                      "the build cannot be identified - staying dormant; game runs "
                      "vanilla.");
            break;
        case builds::MatchResult::Matched:
            break;
    }
}

// Everything up to the point where the mod knows it is running against a build
// it understands. False leaves the mod dormant and the game vanilla; the reason
// is already in the log.
bool SelectBuildProfile() {
    HMODULE host = GetModuleHandleW(nullptr);
    const auto match = builds::SelectProfile(host);
    if (match != builds::MatchResult::Matched) {
        LogDormantReason(match);
        return false;
    }

    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), host, &mi, sizeof(mi))) {
        Log::Line("FATAL: GetModuleInformation failed - cannot resolve RVAs");
        return false;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(mi.lpBaseOfDll);
    ue::SetRuntime(base, base + mi.SizeOfImage, Offsets().UObjectGlobals);
    Log::Line("module base=0x%llx size=0x%x",
        static_cast<unsigned long long>(base), mi.SizeOfImage);
    return true;
}

// Read the ini, write one if there is none, and say what came back.
void LoadConfig() {
    const std::string exeDirA = ExeDirNarrow();
    config_write_default_if_missing(exeDirA);
    config_load(exeDirA, g_config);
    Log::Line("config: udp_port=%d enable=%d local_smoothing=%.2f remote_smoothing=%.2f "
              "position=%d reticle=%d collision=%d",
        g_config.udp_port, g_config.enable_on_startup ? 1 : 0,
        g_config.local_smoothing, g_config.remote_smoothing,
        g_config.position_enabled ? 1 : 0, g_config.reticle_enabled ? 1 : 0,
        g_config.collision_enabled ? 1 : 0);
}

// The receiver and the session that consumes it. Nothing is bound here - see
// BindTracker below for why the socket waits.
void CreateSession() {
    g_receiver = std::make_unique<cameraunlock::UdpReceiver>();
    g_receiver->SetLog([](const std::string& m) { Log::Line("udp: %s", m.c_str()); });
    g_session = std::make_unique<Session>(*g_receiver);
    ApplyConfigToSession();
}

// Open the tracker port. Deliberately after the camera hook is in.
//
// A mod that could not install its hook does nothing with a pose, but the bind
// used to happen first, so a MinHook failure left this process holding UDP 4242
// and a supervisor thread retrying on it for the rest of the session - with the
// mod inert. That port is the one every other head tracking mod and every
// tracker consumer on the machine wants, and a squatted one fails them silently.
//
// A failed bind is still not fatal and nothing below is skipped for it: the
// receiver's own supervisor retries every 500ms and tracking comes up the moment
// the port frees. The reason it failed is on the receiver's own "Failed to bind"
// line, which carries what the OS said - this line must not name a cause of its
// own.
void BindTracker() {
    if (!g_receiver->Start(static_cast<uint16_t>(g_config.udp_port)))
        Log::Line("udp: port %d not bound yet - retrying in the background; "
                  "tracking starts as soon as it binds", g_config.udp_port);
}

// The camera hook and the two things configured on top of it. False when
// MinHook refuses, which it has already said why in the log.
bool InstallHooks() {
    ReticleMover::SetUncappedLog(g_config.pose_log);
    ReticleMover::SetTargets(g_config.reticle_targets.c_str());

    view_hook::Dependencies deps;
    deps.session = g_session.get();
    deps.config = &g_config;
    if (!view_hook::Install(deps)) return false;

    // The build profile picks the render caller; the ini can override it for
    // the diagnostic modes without a rebuild.
    if (g_config.inject_mode >= 0 && g_config.inject_mode < inject::kModeCount) {
        view_hook::SetInjectMode(g_config.inject_mode);
        Log::Line("config: [Diag] InjectMode=%d overrides the profile default",
            g_config.inject_mode);
    }
    return true;
}

DWORD WINAPI BootstrapThread(LPVOID) {
    OpenLog();
    Log::Line("=== Outer Worlds: Spacer's Choice Edition Head Tracking (UE 4.27) ===");
    // The whole no-teardown design rests on this having worked, so it is checked
    // rather than assumed: unpinned, an explicit FreeLibrary reaches the loader-
    // lock deadlock dllmain.cpp describes, and nothing would say why.
    if (!g_pinned)
        Log::Line("WARN: this module could not be pinned, so an unload would run "
                  "thread joins under the loader lock. Nothing unloads an ASI in "
                  "normal play, but a host that does will hang.");

    LoadConfig();
    if (!SelectBuildProfile()) return 0;

    // After the build check, not before. On a build this mod does not know, the
    // process keeps its own unhandled-exception filter and the game's crash
    // reporting is exactly what it would be without the mod present, which is
    // what "runs vanilla" has to mean to be worth saying.
    cameraunlock::diagnostics::InstallCrashHandler();

    CreateSession();
    if (!InstallHooks()) return 0;
    BindTracker();

    // Reported rather than assumed: the poller owns a thread, and a thread that
    // did not start takes every binding with it. Without this the line below
    // would go on naming four keys and four chords that do nothing.
    const bool hotkeys = mod_hotkeys::Register(*g_session, g_config);
    Log::Line("init complete. %s Waiting for OpenTrack on UDP %d.",
        hotkeys ? "End=toggle PageUp=tracking mode PageDown=yaw mode "
                  "(chords Ctrl+Shift+Y/G/H)."
                : "NO HOTKEYS - the poller thread did not start, so no key changes "
                  "anything this session; the mod runs on HeadTracking.ini alone.",
        g_config.udp_port);

    // Last, because it waits for the engine to bring a window up and hold it
    // still, and nothing else in the bootstrap should queue behind that. It only
    // moves a windowed game: a window that fills the work area, or one the game
    // centred itself, is left where it is.
    if (g_config.center_window) CenterWindowWhenReady();
    return 0;
}

// Pin the module so it cannot be unloaded.
//
// dllmain.cpp explains why teardown from DllMain cannot be made safe. The
// receiver and the hotkey poller are namespace-scope objects whose destructors
// join threads, and the CRT runs those from DLL_PROCESS_DETACH through the
// onexit table whether or not DllMain has a case for it.
//
// Pinning removes the FreeLibrary path, which is the one where those joins would
// run under the loader lock against threads that are still alive - the deadlock.
// It does NOT suppress the process-exit path: the module still gets
// DLL_PROCESS_DETACH there and the destructors still run. That path is safe for
// a different reason, worth writing down because it is easy to attribute to the
// pin instead: at process exit the kernel has already terminated every other
// thread, so the joins return immediately. A namespace-scope object whose
// destructor blocks on something other than a dead thread would still hang.
//
// Pinning also keeps the GetPlayerViewPoint trampoline from being left pointing
// into unmapped memory.
void PinModule() {
    HMODULE self = nullptr;
    g_pinned = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN |
                                      GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                  reinterpret_cast<LPCWSTR>(&PinModule),
                                  &self) != FALSE;
}

}  // namespace

void Initialize() {
    PinModule();
    // The handle is closed straight away rather than kept. Nothing waits on this
    // thread - see the note in DllMain about why there is no teardown to wait
    // for - and closing the handle here neither stops the thread nor leaks it.
    HANDLE thread = CreateThread(nullptr, 0, BootstrapThread, nullptr, 0, nullptr);
    if (!thread) {
        // The log file is opened BY that thread, so this cannot go there. Without
        // a line somewhere, a mod that failed to start looks exactly like a mod
        // the ASI loader never loaded, which is the first thing every triage
        // session checks and the most expensive one to get wrong.
        char message[128];
        wsprintfA(message, "HeadTracking: CreateThread failed (error %lu); mod inert\n",
                  GetLastError());
        OutputDebugStringA(message);
        return;
    }
    CloseHandle(thread);
}

}  // namespace tow_ht

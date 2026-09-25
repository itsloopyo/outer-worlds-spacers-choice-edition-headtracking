// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "mod_hotkeys.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "config.h"
#include "inject_mode.h"
#include "logging.h"
#include "view_hook.h"

#include "cameraunlock/input/hotkey_poller.h"
#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"

namespace tow_ht::mod_hotkeys {

namespace {

using cameraunlock::TrackingMode;
using cameraunlock::input::KeyBinding;

std::unique_ptr<cameraunlock::input::HotkeyPoller> g_poller;
Session* g_session = nullptr;

// End changes this session only; EnableOnStartup decides the next one.
void ToggleTracking() {
    const bool nv = !view_hook::TrackingEnabled();
    view_hook::SetTrackingEnabled(nv);
    Log::Line("hotkey: tracking %s", nv ? "ON" : "OFF");
}

// Three-state, not a binary toggle: full -> rotation only -> position only.
void CycleTrackingMode() {
    if (!g_session) return;
    const TrackingMode next = g_session->CycleMode();
    Log::Line("hotkey: tracking mode -> %s",
        next == TrackingMode::RotationAndPosition ? "rotation + position"
        : next == TrackingMode::RotationOnly      ? "rotation only"
                                                  : "position only");
    config::SaveTrackingMode(next);
}

void ToggleYawMode() {
    const bool nv = !view_hook::WorldSpaceYaw();
    view_hook::SetWorldSpaceYaw(nv);
    Log::Line("hotkey: yaw mode %s", nv ? "world" : "local");
    config::SaveWorldSpaceYaw(nv);
}

// Dev: re-confirm the render caller in game after a patch without a rebuild.
void CycleInject() {
    const int m = (view_hook::InjectMode() + 1) % inject::kModeCount;
    view_hook::SetInjectMode(m);
    Log::Line("hotkey: inject mode -> %d", m);
}

// The table's hotkey codec only lets through a list this parser reads.
std::vector<KeyBinding> Bindings(const char* key, const std::string& list) {
    const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(list);
    if (!parsed.ok()) throw std::logic_error(std::string(key) + "='" + list + "': " + parsed.error);
    return parsed.bindings;
}

}  // namespace

bool Register(Session& session, const Config& config) {
    g_session = &session;
    g_poller = std::make_unique<cameraunlock::input::HotkeyPoller>();

    // Each list from HeadTracking.ini. A binding without modifiers does not fire
    // while Ctrl and Shift are both held, so a chord reaches only the action
    // that names it.
    cameraunlock::input::RegisterKeyBindings(*g_poller, Bindings("ToggleKey", config.toggle_key),
                                             [] { ToggleTracking(); });
    cameraunlock::input::RegisterKeyBindings(
        *g_poller, Bindings("CycleTrackingModeKey", config.cycle_tracking_mode_key), [] { CycleTrackingMode(); });
    cameraunlock::input::RegisterKeyBindings(*g_poller, Bindings("YawModeKey", config.yaw_mode_key),
                                             [] { ToggleYawMode(); });
    // Dev only: cycling which GetPlayerViewPoint caller is injected is how the
    // render path is re-identified after a patch. CycleInject wraps, so the
    // whole range is reachable from one key.
    cameraunlock::input::RegisterKeyBindings(*g_poller, Bindings("InjectModeKey", config.inject_mode_key),
                                             [] { CycleInject(); });

    // Checked, not fired and forgotten: without the polling thread every binding
    // registered above is dead and nothing else in the process would notice.
    //
    // Caught rather than tested. HotkeyPoller::Start does not answer false when
    // the thread cannot be created - std::thread's constructor throws and Start
    // rethrows it, and this runs on the bootstrap thread with no handler above
    // it, so an uncaught throw here is std::terminate and the game dies during
    // startup. Which is the opposite of the degradation this branch exists to
    // provide, on the one machine short enough of threads to need it.
    bool started = false;
    try {
        started = g_poller->Start(16);
    } catch (const std::system_error&) {
        started = false;
    }
    if (!started) {
        Log::Line("WARN: the hotkey poller thread did not start, so no hotkey does "
                  "anything this session. Tracking runs on whatever "
                  "HeadTracking.ini says.");
        return false;
    }
    return true;
}

}  // namespace tow_ht::mod_hotkeys

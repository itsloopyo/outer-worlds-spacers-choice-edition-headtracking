// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "mod_hotkeys.h"

#include <system_error>

#include <memory>

#include "ads.h"
#include "config.h"
#include "inject_mode.h"
#include "logging.h"
#include "view_hook.h"

#include "cameraunlock/input/chord_hotkeys.h"
#include "cameraunlock/input/hotkey_poller.h"

namespace tow_ht::mod_hotkeys {

namespace {

using cameraunlock::TrackingMode;
using cameraunlock::input::ChordGuarded;
using cameraunlock::input::NavGuarded;

std::unique_ptr<cameraunlock::input::HotkeyPoller> g_poller;
Session* g_session = nullptr;

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
}

// The mode the frame walk reads once per frame, so the change lands on the aim
// that is already in progress rather than on the next one. Saved as it is
// cycled, because the choice is the player's and a firefight is a bad place to
// lose it.
//
void CycleAdsMode() {
    const AdsMode next = NextAdsModeTwoSlot(GetAdsMode());
    SetAdsMode(next);
    config_save_ads_mode(next);
    Log::Line("hotkey: %s", AdsModeToast(next));
}

void ToggleYawMode() {
    const bool nv = !view_hook::WorldSpaceYaw();
    view_hook::SetWorldSpaceYaw(nv);
    Log::Line("hotkey: yaw mode %s", nv ? "world" : "local");
}

// Dev: re-confirm the render caller in game after a patch without a rebuild.
void CycleInject() {
    const int m = (view_hook::InjectMode() + 1) % inject::kModeCount;
    view_hook::SetInjectMode(m);
    Log::Line("hotkey: inject mode -> %d", m);
}

}  // namespace

bool Register(Session& session, const Config& config) {
    g_session = &session;
    g_poller = std::make_unique<cameraunlock::input::HotkeyPoller>();

    // Nav-cluster (AGENTS.md default bindings). NavGuarded so a chord press
    // cannot also drive the nav path and fire the action twice.
    g_poller->AddHotkey(0x23 /*End*/,      NavGuarded([] { ToggleTracking(); }));
    g_poller->AddHotkey(0x21 /*PageUp*/,   NavGuarded([] { CycleTrackingMode(); }));
    // Page Down unless [Hotkeys] YawModeKey says otherwise; the rest of the
    // cluster is fixed so the same action sits on the same key in every mod.
    g_poller->AddHotkey(config.yaw_mode_key, NavGuarded([] { ToggleYawMode(); }));
    g_poller->AddHotkey(0x2D /*Insert*/,   NavGuarded([] { CycleAdsMode(); }));

    // Ctrl+Shift chord alternatives (Y/G/H/U cluster).
    g_poller->AddHotkey(0x59 /*Y*/, ChordGuarded([] { ToggleTracking(); }));
    g_poller->AddHotkey(0x47 /*G*/, ChordGuarded([] { CycleTrackingMode(); }));
    g_poller->AddHotkey(0x48 /*H*/, ChordGuarded([] { ToggleYawMode(); }));
    g_poller->AddHotkey(0x55 /*U*/, ChordGuarded([] { CycleAdsMode(); }));

    // Dev only, and on the one chord left over rather than a nav key: cycling
    // which GetPlayerViewPoint caller is injected is how the render path is
    // re-identified after a patch.
    //
    // One chord, not the pair it started as. Ctrl+Shift+U is the fleet's ADS
    // slot and is registered above, so leaving the dev "next" on it would fire
    // both handlers on one press. CycleInject wraps, so the whole range is still
    // reachable from Ctrl+Shift+J alone.
    g_poller->AddHotkey(0x4A /*J*/, ChordGuarded([] { CycleInject(); }));

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
        Log::Line("WARN: the hotkey poller thread did not start, so End, Page Up, "
                  "Page Down, Insert and the Ctrl+Shift chords do nothing this "
                  "session. Tracking runs on whatever HeadTracking.ini says.");
        return false;
    }
    return true;
}

}  // namespace tow_ht::mod_hotkeys

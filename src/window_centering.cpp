// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "window_centering.h"

#include "logging.h"

#include "cameraunlock/os/game_window.h"

#include <cstdlib>

#include <windows.h>

namespace tow_ht {

namespace {

namespace os = cameraunlock::os;

constexpr int kPollIntervalMs = 250;
constexpr int kPollAttempts = 240;  // 60s, which covers a cold start off a hard disk.

// The rect has to hold still before it is worth acting on. The window is up
// well before the engine has finished sizing and placing it, and this mod has
// not measured when the game stops moving it, so the wait is on three seconds
// of an unchanged rect rather than on a fixed delay that would be a guess.
constexpr int kSettlePolls = 12;

// The window this settled on, and what it is, so the log names which window was
// inspected rather than only what was done to it. FindGameWindow takes the first
// visible unowned top-level window of this process in Z-order, and a game can
// have more than one.
void DescribeWindow(HWND window, const RECT& rect) {
    constexpr int kClassChars = 64;
    wchar_t cls[kClassChars] = L"";
    GetClassNameW(window, cls, kClassChars);
    // Four bytes per wide character, so the conversion cannot run out of room,
    // and the buffer is zero-initialised so a conversion that writes nothing
    // still leaves an empty C string.
    char narrow[kClassChars * 4] = "";
    WideCharToMultiByte(CP_UTF8, 0, cls, -1, narrow, static_cast<int>(sizeof(narrow)),
                        nullptr, nullptr);
    Log::Line("window: settled on hwnd=0x%llx class=%s %dx%d at (%d, %d)",
        reinterpret_cast<unsigned long long>(window), narrow,
        static_cast<int>(rect.right - rect.left),
        static_cast<int>(rect.bottom - rect.top),
        static_cast<int>(rect.left), static_cast<int>(rect.top));
}

// Never past the start of the work area. A window taller than the work area -
// a 720p client on a 768-tall laptop once the taskbar is taken off, which is
// 759 rows of window in 672 rows of space - centres to a NEGATIVE origin, and
// moving it there puts the title bar above the visible desktop where it cannot
// be grabbed and dragged back. That is the harm this whole function is willing
// to move a window in order to avoid.
int CenteredOrigin(int areaStart, int areaExtent, int windowExtent) {
    const int origin = areaStart + (areaExtent - windowExtent) / 2;
    return origin < areaStart ? areaStart : origin;
}

// A fullscreen or borderless window covers the whole monitor. It is centred by
// definition and where it sits is the game's decision, so it is left alone.
//
// This is NOT the maximized test: a maximized window is the work area grown by
// the border, so it does not reach the monitor's bottom edge while a taskbar is
// showing. Maximized is caught by the work-area test below instead, which it
// passes exactly.
bool CoversMonitor(const RECT& window, const RECT& monitor) {
    return window.left <= monitor.left && window.top <= monitor.top &&
           window.right >= monitor.right && window.bottom >= monitor.bottom;
}

bool IsCenteredOn(const RECT& window, const RECT& area) {
    // A game that centres its own window rounds the odd half-pixel up where the
    // integer maths here rounds it down, so an exact comparison would move the
    // window one pixel and report that as a fix.
    constexpr int kTolerance = 2;
    const int dx = window.left -
                   CenteredOrigin(area.left, area.right - area.left, window.right - window.left);
    const int dy = window.top -
                   CenteredOrigin(area.top, area.bottom - area.top, window.bottom - window.top);
    return std::abs(dx) <= kTolerance && std::abs(dy) <= kTolerance;
}

// Centre the window this function was handed, rather than asking the OS for a
// game window a second time. Core's CenterGameWindowOnce re-runs its own
// EnumWindows and its own GetWindowRect, so delegating to it would decide on one
// window and move whichever one that later search returned, and would apply a
// second, different "leave it alone" rule (it declines on a window that fills
// the work area, this declines on one already centred) with neither able to see
// the other's verdict.
void CenterUnlessAlready(HWND window, const RECT& rect) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info)) {
        Log::Line("WARN: window: GetMonitorInfoW failed: %lu", GetLastError());
        return;
    }

    if (CoversMonitor(rect, info.rcMonitor)) {
        Log::Line("window: covers the whole monitor, so it is fullscreen or "
                  "borderless - leaving it alone");
        return;
    }
    // Against the WORK AREA only, which is also where it would be moved to. A
    // window a game centred on the monitor is half a taskbar low, and on a
    // top-docked taskbar that puts its title bar underneath one, where it cannot
    // be dragged back - so that case is worth the small jump rather than exempt
    // from it.
    if (IsCenteredOn(rect, info.rcWork)) {
        Log::Line("window: already centred on the work area, leaving it alone");
        return;
    }

    const int width = static_cast<int>(rect.right - rect.left);
    const int height = static_cast<int>(rect.bottom - rect.top);
    const int x = CenteredOrigin(info.rcWork.left, info.rcWork.right - info.rcWork.left, width);
    const int y = CenteredOrigin(info.rcWork.top, info.rcWork.bottom - info.rcWork.top, height);

    // SWP_ASYNCWINDOWPOS because the window belongs to another thread. Without
    // it SetWindowPos SENDS the position messages and blocks until that thread
    // pumps, which is a wait this thread has no way out of if the game is busy.
    if (!SetWindowPos(window, nullptr, x, y, 0, 0,
                      SWP_ASYNCWINDOWPOS | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)) {
        Log::Line("WARN: window: SetWindowPos failed: %lu", GetLastError());
        return;
    }
    Log::Line("window: centred %dx%d window at (%d, %d)", width, height, x, y);
}

// Waits for a game window whose HANDLE and rect have both held still for
// kSettlePolls, writing them to the out-params. False when none settled in
// time, in which case the out-params are left alone.
//
// The handle is compared as well as the rect: a rect that has not changed is
// not the same claim as a window that has not changed, and the game can put up
// more than one window that satisfies the search.
bool WaitForSettledWindow(HWND& settled, RECT& settledRect) {
    HWND previousWindow = nullptr;
    RECT previous{};
    int stablePolls = 0;

    for (int attempt = 0; attempt < kPollAttempts; ++attempt) {
        Sleep(kPollIntervalMs);

        const HWND window = os::FindGameWindow();
        RECT current{};
        if (!window || !GetWindowRect(window, &current)) {
            previousWindow = nullptr;
            stablePolls = 0;
            continue;
        }

        if (window == previousWindow && EqualRect(&previous, &current)) {
            if (++stablePolls < kSettlePolls) continue;
            settled = window;
            settledRect = current;
            return true;
        }
        previousWindow = window;
        previous = current;
        stablePolls = 0;
    }
    return false;
}

}  // namespace

void CenterWindowWhenReady() {
    HWND window = nullptr;
    RECT rect{};
    if (!WaitForSettledWindow(window, rect)) {
        Log::Line("window: no window settled within %ds, leaving placement alone",
                  kPollAttempts * kPollIntervalMs / 1000);
        return;
    }
    DescribeWindow(window, rect);
    CenterUnlessAlready(window, rect);
}

}  // namespace tow_ht

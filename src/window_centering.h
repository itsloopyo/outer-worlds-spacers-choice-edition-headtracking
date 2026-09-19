// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace tow_ht {

// Waits for the game to bring its window up and hold it still, then centres it
// on the work area of the monitor it is already on. Only a windowed game is ever
// moved: one that covers the whole monitor is fullscreen or borderless and is
// left alone, as is one already centred on the work area.
//
// A window the game centred on the whole MONITOR is not exempt - it sits half a
// taskbar low, which on a top-docked taskbar puts its title bar underneath one -
// so it is nudged onto the work-area centre.
//
// Blocks until the window settles or the wait times out, so call it after
// everything else the bootstrap thread has to do. Bounded: it gives up after
// kPollAttempts and returns, so it cannot hold the bootstrap thread open for the
// life of the process.
void CenterWindowWhenReady();

}  // namespace tow_ht

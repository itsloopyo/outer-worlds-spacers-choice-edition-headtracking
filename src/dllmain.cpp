// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "headtracking_mod.h"

#include <windows.h>

// There is no DLL_PROCESS_DETACH case, and that is the whole design.
//
// Everything this mod would want to tear down - the hotkey poller, the UDP
// receiver and its supervisor, MinHook's trampolines - is owned by a thread, and
// every way of tearing one down from DllMain deadlocks. DllMain runs holding the
// loader lock; a thread cannot finish exiting without that same lock, so a join
// here waits forever on a thread that is waiting on us. MinHook's own
// MH_Uninitialize is worse again: it takes a module snapshot and suspends every
// other thread in the process, both under the lock. Bounding the wait does not
// fix it either - it converts a hang into an unmap of a module whose threads are
// still running in it.
//
// The honest answer is that this DLL does not support being unloaded. An ASI
// loader loads it once and the process keeps it to exit, where the kernel has
// already stopped every other thread and reclaims the rest. The log is written
// unbuffered, so nothing is lost by never closing it.
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        tow_ht::Initialize();
    }
    return TRUE;
}

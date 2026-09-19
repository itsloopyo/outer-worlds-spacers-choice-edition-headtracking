// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once
#include <windows.h>

namespace tow_ht {

// The one entry point DllMain calls. It spins up a bootstrap thread so the heavy
// work (config, fingerprinting, UDP, MinHook) never runs under the loader lock,
// and returns immediately.
//
// There is deliberately no Shutdown() - see the note in dllmain.cpp.
void Initialize();

}

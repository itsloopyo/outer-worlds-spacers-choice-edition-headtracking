// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once
#include <windows.h>
#include "build_profile.h"

// Profile registry and selection. SelectProfile() fingerprints the host EXE
// (PE TimeDateStamp + SizeOfImage + CheckSum) and installs the matching profile
// as active. Unlisted builds use validated runtime discovery.

namespace tow_ht
{
    namespace builds
    {
        enum class MatchResult
        {
            Matched,     // Active profile set; mod can run.
            ReadFailed,  // Could not read the PE header.
            DiscoveryFailed,
        };

        MatchResult SelectProfile(HMODULE host);
        const BuildProfile& ActiveProfile();
        bool UsesRuntimeDiscovery();
        std::uintptr_t SceneViewRva();
    }

    // Accessor for the active profile's offset table. Must run after
    // SelectProfile() returns Matched.
    inline const OffsetTable& Offsets()
    {
        return builds::ActiveProfile().Offsets;
    }
}

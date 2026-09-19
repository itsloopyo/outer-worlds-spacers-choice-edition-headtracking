// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once
#include <windows.h>
#include "build_profile.h"

// Profile registry and selection. SelectProfile() fingerprints the host EXE
// (PE TimeDateStamp + SizeOfImage + CheckSum) and installs the matching profile
// as active, or stays dormant if no profile claims this build.

namespace tow_ht
{
    namespace builds
    {
        enum class MatchResult
        {
            Matched,     // Active profile set; mod can run.
            ReadFailed,  // Could not read the PE header.
            HostNewer,   // Running EXE TimeDateStamp > primary profile.
            HostOlder,   // Running EXE TimeDateStamp < primary profile.
            HostDiffers, // Same timestamp, different size or checksum.
            ProfileIncomplete, // Fingerprint matched a profile whose offsets are TBD.
        };

        MatchResult SelectProfile(HMODULE host);
        const BuildProfile& ActiveProfile();
    }

    // Accessor for the active profile's offset table. Must run after
    // SelectProfile() returns Matched.
    inline const OffsetTable& Offsets()
    {
        return builds::ActiveProfile().Offsets;
    }
}

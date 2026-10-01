// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_registry.h"

#include <array>
#include <vector>

#include <cameraunlock/memory/pe_fingerprint.h>

#include "logging.h"
#include "runtime_discovery.h"

namespace tow_ht::builds
{
    // Keep the existing profiles usable even if an older compiler emitted
    // code that the runtime discovery does not recognise.
    extern const BuildProfile kSteamProfile_20260804;
    extern const BuildProfile kSteamProfile_20260505;

    namespace
    {
        constexpr std::array<const BuildProfile*, 2> kKnownProfiles = {
            &kSteamProfile_20260804,
            &kSteamProfile_20260505,
        };

        const BuildProfile* g_active = nullptr;
        BuildProfile g_discovered{};
        std::uint32_t g_sceneView = 0;

        bool ResolveScene(HMODULE host, std::uint32_t size) {
            std::vector<std::uint8_t> image(size);
            SIZE_T copied = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), host, image.data(), image.size(), &copied) ||
                copied != image.size()) {
                Log::Line("discovery: scene-view snapshot failed: Win32 error %lu", GetLastError());
                return false;
            }
            std::string reason;
            if (!DiscoverSceneView({image.data(), image.size(), reinterpret_cast<std::uintptr_t>(host)},
                static_cast<std::uint32_t>(g_active->Offsets.kKnownCallerRvas[0]), g_sceneView, reason)) {
                Log::Line("discovery: %s", reason.c_str());
                return false;
            }
            Log::Line("discovery: scene-view construction validated at RVA 0x%08x", g_sceneView);
            return true;
        }

        // A profile is "complete" iff it carries both the hook target and the
        // gameplay gate. Lets a profile with the correct fingerprint but RVAs
        // still TBD register without risking activation against stale/zero
        // addresses.
        //
        // The gate offset is in the test because a profile carrying one without
        // the other is the shape that produces a half-working mod: hooked, and
        // applying head tracking through the pause menu, dialogue, the inventory
        // and every loading screen because the one read that says otherwise
        // lands on the controller's vtable pointer instead. Dormant is the
        // honest answer for a build only half derived.
        bool ProfileIsComplete(const BuildProfile* p)
        {
            return p && p->Offsets.kGetPlayerViewPointRva != 0
                     && p->Offsets.kShowMouseCursorOffset != 0;
        }
    }

    MatchResult SelectProfile(HMODULE host)
    {
        PeFingerprint running{};
        if (!cameraunlock::memory::ReadPeFingerprint(host, running)) {
            Log::Line("build-check: failed to read PE header from host module");
            return MatchResult::ReadFailed;
        }

        Log::Line("build-check: running  ts=0x%08x size=0x%08x csum=0x%08x",
            running.TimeDateStamp, running.SizeOfImage, running.CheckSum);

        for (const BuildProfile* p : kKnownProfiles) {
            const bool complete = ProfileIsComplete(p);
            Log::Line("build-check: profile=%s ts=0x%08x size=0x%08x csum=0x%08x%s",
                p->Name, p->Fingerprint.TimeDateStamp,
                p->Fingerprint.SizeOfImage, p->Fingerprint.CheckSum,
                complete ? "" : " (incomplete - offsets TBD)");
            if (running.Matches(p->Fingerprint)) {
                if (!complete) {
                    Log::Line("build-check: profile %s has no offsets; trying runtime discovery", p->Name);
                    break;
                }
                g_active = p;
                if (!ResolveScene(host, running.SizeOfImage)) return MatchResult::DiscoveryFailed;
                Log::Line("build-check: matched profile %s", p->Name);
                return MatchResult::Matched;
            }
        }

        Log::Line("build-check: no complete profile matched; discovering engine addresses");
        std::vector<std::uint8_t> image(running.SizeOfImage);
        SIZE_T copied = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), host, image.data(), image.size(), &copied) ||
            copied != image.size()) {
            Log::Line("discovery: could not snapshot the executable: Win32 error %lu", GetLastError());
            return MatchResult::DiscoveryFailed;
        }
        std::string reason;
        g_discovered = {"runtime-discovered", running, {}};
        if (!DiscoverOffsets({image.data(), image.size(), reinterpret_cast<std::uintptr_t>(host)},
                             g_discovered.Offsets, reason)) {
            Log::Line("discovery: %s", reason.c_str());
            return MatchResult::DiscoveryFailed;
        }
        g_active = &g_discovered;
        if (!ResolveScene(host, running.SizeOfImage)) return MatchResult::DiscoveryFailed;
        const auto& offsets = g_discovered.Offsets;
        Log::Line("discovery: validated view=0x%08llx render=0x%08llx objects=0x%08llx "
                  "names=0x%08llx event=0x%08llx cursor=0x%zx",
            static_cast<unsigned long long>(offsets.kGetPlayerViewPointRva),
            static_cast<unsigned long long>(offsets.kKnownCallerRvas[0]),
            static_cast<unsigned long long>(offsets.UObjectGlobals.kObjObjects),
            static_cast<unsigned long long>(offsets.UObjectGlobals.kFNamePool),
            static_cast<unsigned long long>(offsets.kProcessEventRva), offsets.kShowMouseCursorOffset);
        return MatchResult::Matched;
    }

    const BuildProfile& ActiveProfile() { return *g_active; }
    bool UsesRuntimeDiscovery() { return g_active == &g_discovered; }
    std::uintptr_t SceneViewRva() { return g_sceneView; }
}

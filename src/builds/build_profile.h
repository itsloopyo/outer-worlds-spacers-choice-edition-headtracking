// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

#include <cameraunlock/memory/pe_fingerprint.h>
#include <cameraunlock/unreal/ue_runtime.h>

#include "inject_mode.h"

// One BuildProfile describes a single shipped build of Outer Worlds: Spacer's Choice Edition:
// the PE-header fingerprint that uniquely identifies it, plus every per-build
// RVA / field offset the camera hook needs. The registry holds one profile per
// supported build; at startup the mod fingerprints the live module and selects
// the matching profile. No match leaves the mod fully dormant (no hooks
// installed, game runs vanilla) - see AGENTS.md "Maintain compatibility across
// new patches": never edit an existing profile's RVAs in place, ADD a new one.
//
// Outer Worlds: Spacer's Choice Edition is UE 4.27 (pre-LWC: FVector/FRotator
// are 3-float, 12 bytes). The offset set injects the head pose into the
// render-path view and leaves every other GetPlayerViewPoint caller clean (the
// aim/interaction-trace decoupling), so weapon fire and interaction traces read
// the clean mouse/pad aim while the player sees the head-tracked view.

namespace tow_ht
{
    // PE-header build fingerprint (TimeDateStamp + SizeOfImage + CheckSum);
    // the shared type keeps reading/matching/classification in core.
    using PeFingerprint = ::cameraunlock::memory::PeFingerprint;

    // Where UE keeps a reflected field's offset. Engine-version-bound rather
    // than build-bound - every UE 4.27 game has the same numbers - but they
    // live in the profile because the next engine version will not, and a
    // silently wrong offset here reads a property table as noise. The mod
    // hardcodes no other struct layout: every parameter frame and field it
    // touches is looked up by name in the engine's own reflection data, and
    // these are the offsets needed to READ that data.
    struct ReflectionLayout
    {
        // FField, the base of every FProperty since UE 4.25.
        std::size_t kFField_ClassPrivate;   // FFieldClass*
        std::size_t kFField_Next;           // FField*
        std::size_t kFField_NamePrivate;    // FName

        // FProperty, on top of FField.
        std::size_t kFProperty_ArrayDim;
        std::size_t kFProperty_ElementSize;
        std::size_t kFProperty_Offset;      // Offset_Internal

        // FFieldClass, reached through FField::ClassPrivate. Its name says
        // whether a property is a BoolProperty, a StructProperty and so on.
        std::size_t kFFieldClass_Name;

        // UStruct, the base of UClass, UScriptStruct and UFunction.
        std::size_t kUStruct_SuperStruct;
        std::size_t kUStruct_ChildProperties;  // FField* chain
        std::size_t kUStruct_PropertiesSize;   // int32
    };

    struct OffsetTable
    {
        // Hook target: APlayerController::GetPlayerViewPoint. RVA from the
        // module base. Zero = profile incomplete (mod stays dormant).
        std::uintptr_t kGetPlayerViewPointRva;

        // Return-address RVAs of the distinct GetPlayerViewPoint call sites.
        // Head tracking is injected ONLY for callers flagged here per the
        // active inject mode; every other caller reads the clean (mouse/pad)
        // rotation. That per-caller gate IS the look/aim decoupling.
        // 0-valued trailing entries are unused padding.
        //
        // inject::CallerRvas rather than a std::array spelled out here: the
        // gate indexes this by mode number, so a slot count that disagreed
        // with inject::kCallerSlots would read past the end.
        inject::CallerRvas kKnownCallerRvas;

        // Default inject mode at startup. 0 = all callers (diagnostic only),
        // 1..16 = inject only for kKnownCallerRvas[mode-1] (the render-path
        // caller / FMinimalViewInfo builder), 17 = none. Page Down / Page Up
        // cycle this live so the render caller can be re-confirmed in game
        // after a patch without a rebuild.
        int kDefaultInjectMode;

        // APlayerController::bShowMouseCursor bitfield, for the InGameplay
        // gate (cursor visible == menu/cutscene -> suppress tracking). Both
        // zero = gate disabled (always treat as gameplay).
        std::size_t   kShowMouseCursorOffset;
        std::uint32_t kShowMouseCursorMask;

        // FMinimalViewInfo field offsets. The render caller is
        // ULocalPlayer::GetViewPoint, which hands GetPlayerViewPoint pointers
        // to the Location and Rotation fields of the FMinimalViewInfo it is
        // filling in, so the live FOV the frame will render with sits at
        // outLocation + kFovOffset. kRotationStride is the Location->Rotation
        // gap, checked against the actual out-param pair before the FOV is
        // read: two pointers that are not that far apart are not fields of one
        // view info, and what looks like the FOV is a local in some other
        // caller's stack frame.
        struct {
            std::size_t kFovOffset;
            std::size_t kRotationStride;
            std::size_t kAspectRatioOffset;
        } MinimalViewInfoLayout;

        // GUObjectArray / FNamePool, for finding UMG widgets, UFunctions and
        // UScriptStructs by name. The shared core type is what ue::SetRuntime
        // consumes.
        ::cameraunlock::unreal::UObjectGlobalsLayout UObjectGlobals;

        // UObject::ProcessEvent, for dispatching the UFUNCTIONs the reticle
        // mover, the aim trace and the lean sweep call. Pinned by RVA rather
        // than read from a vtable slot because AActor overrides the slot with
        // an RPC-aware variant, and the base UObject one is what must be
        // called here.
        std::uintptr_t kProcessEventRva;

        // How to read the engine's reflection data. See ReflectionLayout.
        ReflectionLayout Reflection;
    };

    struct BuildProfile
    {
        const char*   Name;
        PeFingerprint Fingerprint;
        OffsetTable   Offsets;
    };
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_profile.h"

// Every Steam build of The Outer Worlds: Spacer's Choice Edition this mod knows
// about (Indiana/Binaries/Win64/Indiana-Win64-Shipping.exe, UE 4.27).
// Append-only: when a patch breaks the RVAs below, ADD a kSteamProfile_<date>
// and put it at the top of kKnownProfiles in build_registry.cpp. Never edit an
// existing profile's numbers - a player who has not taken the patch still
// matches their old profile by fingerprint. See AGENTS.md "Maintain
// compatibility across new patches".
//
// UE 4.27 predates Large World Coordinates: FVector / FRotator / FVector2D are
// floats, not the UE5 doubles. Every frame this mod writes into engine memory
// is sized accordingly.

namespace tow_ht::builds
{
    extern const BuildProfile kSteamProfile_20260804;
    extern const BuildProfile kSteamProfile_20260505;

    // ---- Steam Win64, PE TimeDateStamp 0x6A7165A7, linked 2026-08-04 ----
    const BuildProfile kSteamProfile_20260804 = {
        /* Name        */ "steam-win64-20260804",
        /* Fingerprint */ { 0x6A7165A7u, 0x06A74000u, 0x0678EBCCu },
        /* Offsets     */ {
            // APlayerController::GetPlayerViewPoint.
            //
            // This build carries no "GetPlayerViewPoint" symbol or narrow
            // string, so the identification rests on two facts that agree.
            // GetActorEyesViewPoint sits at vtable slot 218, which is the slot
            // execGetActorEyesViewPoint dispatches to and the one name AActor's
            // native-registration table still carries. AController's forwarder
            // to that slot occupies a single controller vtable entry, slot 258,
            // so slot 258 of AIndianaPlayerController is
            // APlayerController::GetPlayerViewPoint.
            //
            // Its behaviour matches UE 4.27's own: a cached-POV fast path
            // copying controller+0x3b0 and +0x3bc into the two out-params, else
            // PlayerCameraManager at controller+0x350 with the
            // CameraCachePrivate.TimeStamp > 0 test at manager+0x1bc0 before
            // dispatching GetCameraViewPoint through the manager's own vtable.
            /* kGetPlayerViewPointRva */ 0x03651980ULL,

            // Observed in game, never derived statically: GetPlayerViewPoint is
            // dispatched through the vtable, so it has no static call sites to
            // enumerate, and displacement 0x810 means something else entirely
            // on a camera manager's vtable (GetCameraLocation) - a static sweep
            // for the displacement returns 47 sites, most of them nothing to do
            // with a controller. These are the distinct return RVAs an
            // inject-mode-0 caller summary saw, render caller first so
            // inject::kFirstCaller selects it.
            //
            // Slot 1 is ULocalPlayer::GetViewPoint on positive evidence rather
            // than by elimination: its call site reads PlayerController off the
            // local player at +0x38, the camera manager off the controller at
            // +0x350, copies GetCameraCacheView's whole FMinimalViewInfo into
            // OutViewInfo, calls GetFOVAngle through the manager's vtable at
            // +0x7c8 and stores it at OutViewInfo+0x18, then passes
            // OutViewInfo+0x00 in rdx and OutViewInfo+0x0c in r8 as the two
            // out-params - independently reproducing every FMinimalViewInfo
            // offset in this profile. It is also the single funnel UE uses for
            // both CalcSceneView and GetProjectionData, so injecting there
            // moves the drawn frame AND the projection every world-anchored UI
            // element goes through.
            /* kKnownCallerRvas */ {{
                0x034a6b1aULL,  // 1: ULocalPlayer::GetViewPoint - RENDER PATH
                0x035562d8ULL,  // 2: fn 0x03556204
                0x0364d044ULL,  // 3: fn 0x0364ced7
                0x03653320ULL,  // 4: fn 0x03653300, appends the view point to a TArray
                0x033bddb8ULL,  // 5: fn 0x033bdd82
                0x00baf2d3ULL,  // 6: fn 0x00baf040
                0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL,
                0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL,
            }},
            /* kDefaultInjectMode     */ inject::kFirstCaller,

            // APlayerController::bShowMouseCursor. The property itself is not
            // registered in this build - "bShowMouseCursor" appears nowhere in
            // the exe - so the offset comes from its neighbours, which are:
            // the generated SetBitFunc stubs for bEnableClickEvents,
            // bEnableTouchEvents and bEnableMouseOverEvents are one instruction
            // each, and they OR bits 2, 4 and 8 into the dword at
            // controller+0x4e0, in APlayerController's declaration order.
            // bShowMouseCursor is declared immediately before the three of
            // them, so it is bit 1 of that same dword. The same records give
            // APlayerController's own size as 0x628, which contains every other
            // controller offset this profile carries.
            /* kShowMouseCursorOffset */ 0x4e0,
            /* kShowMouseCursorMask   */ 0x1u,

            // FMinimalViewInfo, read straight out of ULocalPlayer::GetViewPoint
            // rather than from a property table: it copies 12 bytes to
            // OutViewInfo+0x00 and 12 to +0x0c from the camera cache, stores
            // GetFOVAngle's return at +0x18, copies +0x1c..+0x2c across, then
            // lea's +0x40 for the FPostProcessSettings copy. Location 0x00,
            // Rotation 0x0c, FOV 0x18, AspectRatio 0x2c - the 4-byte-per-float
            // pre-LWC layout.
            /* MinimalViewInfoLayout  */ { 0x18, 0x0c, 0x2c },

            // GUObjectArray and the FNamePool.
            //
            // FUObjectArray::AllocateUObjectIndex is the function carrying the
            // "Unable to add more objects to disregard for GC pool" fatal; its
            // single caller lea's 0x06307290 into rcx as the `this`, so that is
            // GUObjectArray. ObjObjects is the member at +0x10, and the proof
            // it is the right field is the read count: 1198 rip-relative loads
            // reference 0x063072a0 against 70 for the array base.
            //
            // NamePoolData is the .data address lea'd into rcx before the
            // FNamePool constructor, which is identified as the constructor by
            // being the one function referencing the hardcoded name strings
            // ("ByteProperty", "IntProperty", ...).
            /* UObjectGlobals */ {
                0x063072a0ULL,      // kObjObjects (GUObjectArray 0x06307290 + 0x10)
                0x14,               // kObjObjects_Num
                0x18,               // kFUObjectItemSize
                0x10000,            // kChunkNumElems
                0x062cae80ULL,      // kFNamePool
                0x10,               // kFNamePoolBlocks
                0x10,               // kClassPrivate
                0x18,               // kNamePrivate
                0x20,               // kOuterPrivate
            },

            // UObject::ProcessEvent. Opens by testing UFunction::FunctionFlags
            // for FUNC_Net at +0xb8, and appears in 244 vtables - it is the
            // base implementation every class that does not override inherits.
            /* kProcessEventRva */ 0x03139610ULL,

            // ReflectionLayout, UE 4.27, and NOT a UE 5.5 sibling mod's
            // numbers: every field below sits 8 bytes higher than bodycam's
            // equivalent, because 4.27's FFieldVariant is still a pointer/bool
            // pair rather than the tagged pointer UE 5.5 packs it into. Copying
            // those in reads the property chain as noise - which is exactly
            // what the first attempt here did, reporting FMinimalViewInfo's
            // PropertiesSize as 1916580736.
            //
            // The three UStruct offsets were read off the live object: dumping
            // the UScriptStruct for MinimalViewInfo shows the FStructBaseChain
            // pointer at +0x38, then SuperStruct 0 at +0x48, Children 0 at
            // +0x50 (4.25+ moved properties off the UField chain), the
            // ChildProperties pointer at +0x58, and PropertiesSize 0x600 with
            // MinAlignment 0x10 packed into +0x60.
            //
            // Checked at startup against the same struct's known layout -
            // Location 0x00, Rotation 0x0c, FOV 0x18, AspectRatio 0x2c, all
            // read out of ULocalPlayer::GetViewPoint itself - and
            // every reflection-backed feature stands down if they disagree.
            /* Reflection */ {
                0x08,               // kFField_ClassPrivate
                0x20,               // kFField_Next
                0x28,               // kFField_NamePrivate
                0x38,               // kFProperty_ArrayDim
                0x3c,               // kFProperty_ElementSize
                0x4c,               // kFProperty_Offset
                0x00,               // kFFieldClass_Name
                0x48,               // kUStruct_SuperStruct
                0x58,               // kUStruct_ChildProperties
                0x60,               // kUStruct_PropertiesSize
            },
        },
    };

    // ---- Steam Win64, PE TimeDateStamp 0x69FA7523, linked 2026-05-05 ----
    //
    // Kept with its offsets still zero: nothing was ever derived against this
    // binary, so the mod stays dormant for a player who is on it rather than
    // hooking addresses that belong to a different build.
    const BuildProfile kSteamProfile_20260505 = {
        /* Name        */ "steam-win64-20260505",
        /* Fingerprint */ { 0x69FA7523u, 0x06A68000u, 0x0677E2A7u },
        /* Offsets     */ {
            /* kGetPlayerViewPointRva */ 0x0ULL,
            /* kKnownCallerRvas */ {{
                0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL,
                0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL,
            }},
            /* kDefaultInjectMode     */ inject::kFirstCaller,
            /* kShowMouseCursorOffset */ 0x0,
            /* kShowMouseCursorMask   */ 0x1u,
            /* MinimalViewInfoLayout  */ { 0x0, 0x0, 0x0 },
            /* UObjectGlobals         */ { 0, 0, 0, 0, 0, 0, 0, 0, 0 },
            /* kProcessEventRva       */ 0x0ULL,
            /* Reflection             */ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        },
    };
}

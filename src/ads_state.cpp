// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "ads_state.h"

#include "logging.h"
#include "ue_reflect.h"
#include "ue_vm.h"

#include "cameraunlock/memory/safe_memory.h"
#include "cameraunlock/unreal/ue_runtime.h"

namespace tow_ht::ads_state {

namespace {

namespace ue = ::cameraunlock::unreal;

// AIndianaPlayerCharacter's first-person skeletal mesh, the animation instance
// running on it, and the fine-aim flag that instance carries. All three are
// asked for by name, so a patch that moves them is followed.
constexpr const char* kFpvMeshName = "FPVMesh";
constexpr const char* kAnimInstanceName = "AnimScriptInstance";
constexpr const char* kAimFlagName = "bIsFineAiming";

ue_vm::ResolveRetry g_resolveRetry;

// Every verdict here is keyed on a CLASS, never on the session and never on an
// object pointer.
//
// A pawn without FPVMesh is not the player character - it is the pawn the game
// possesses at the menu, across a travel before possession completes, or in the
// character creator. The honest answer for one of those is "not aiming".
// Standing the probe down for the whole session instead would hand the player
// hip-fire behaviour through every aim of every firefight afterwards, off one
// frame at a menu, with the single log line that said so long since scrolled
// away.
//
// Keying on the class rather than the pawn POINTER matters for the same reason
// pointed the other way: UE recycles UObject slots, so a later pawn landing on
// an address seen before would be read at the previous class's offsets.
std::uintptr_t g_pawnClass = 0;
std::size_t g_fpvMeshOffset = 0;
std::size_t g_animInstanceOffset = 0;
bool g_havePawnOffsets = false;
// The pawn classes already written off, as a bounded ring of four.
//
// A single slot was wrong in both directions. Kept for the session it goes on
// matching whatever class UE later builds into a freed UObject slot, so one
// menu or travel pawn with no FPVMesh could reject the real player character
// for the rest of the run. Cleared on every pawn change it re-probes and
// re-DumpProperties the same class on every fast travel, and the "one line per
// class, then silence" contract goes with it.
//
// Four entries covers a player pawn alternating with the transient ones and
// still ages a stale address out after four more classes have been written off.
constexpr std::size_t kUnsupportedPawnSlots = 4;
std::uintptr_t g_unsupportedPawnClasses[kUnsupportedPawnSlots] = {};
std::size_t g_unsupportedPawnNext = 0;

bool PawnClassWrittenOff(std::uintptr_t cls) {
    for (std::uintptr_t seen : g_unsupportedPawnClasses)
        if (seen != 0 && seen == cls) return true;
    return false;
}

void WriteOffPawnClass(std::uintptr_t cls, const char* what) {
    if (PawnClassWrittenOff(cls)) return;
    g_unsupportedPawnClasses[g_unsupportedPawnNext] = cls;
    g_unsupportedPawnNext = (g_unsupportedPawnNext + 1) % kUnsupportedPawnSlots;
    Log::Line("ads: %s. Head tracking behaves as though the sights are never raised "
              "while this pawn is possessed, which is hip-fire behaviour through an "
              "aim rather than a crash.", what);
}

// The animation instance is NOT cached - it is read through the mesh every
// frame, because a graph re-initialised mid-session replaces the object without
// touching the pawn, and a cached pointer would then be read for the rest of the
// session. What is cached is the flag's offset, keyed on the class it was
// resolved against, so a replacement of a different class re-resolves.
std::uintptr_t g_animClass = 0;
std::size_t g_aimFlagOffset = 0;
std::uintptr_t g_unsupportedAnimClass = 0;
// The animation class the last frame actually saw, whether or not it resolved.
// g_animClass records a SUCCESS, so it is the wrong thing to test a stand-down
// against - after one, it still holds whatever last worked, or zero.
std::uintptr_t g_observedAnimClass = 0;

// How many times in a row a class has to fail before it is written off.
//
// One is not enough. FindPropertyInChain answers false for a guarded read that
// FAULTED as well as for a property that is genuinely absent, and a single read
// fault while a class object is being touched would otherwise write off the
// player's own character for the session - the exact outcome the class-scoping
// was introduced to avoid, arriving by a different route.
//
// Every one of the five stand-downs is behind a ResolveRetry, so four failures
// is about a second of the class agreeing with itself rather than four frames.
// That distinction matters: at 144Hz four frames is 28ms, which a graph
// re-initialisation outlasts easily.
constexpr int kFailuresBeforeStandDown = 4;

// The flag lookup and the value check get their own retry rather than sharing
// the pawn's. ResolveFlag does not cache a failure, so without this it walks the
// whole FField chain on every rendered frame while it is failing.
ue_vm::ResolveRetry g_flagRetry;

std::uintptr_t g_failingPawnClass = 0;
int g_pawnFailures = 0;
std::uintptr_t g_failingAnimClass = 0;
int g_animFailures = 0;

// True once `cls` has failed kFailuresBeforeStandDown times running. A different
// class arriving restarts the count, so an interleaved pawn cannot push another
// one over the line.
bool RepeatedFailure(std::uintptr_t& failingSlot, int& count, std::uintptr_t cls) {
    if (failingSlot != cls) {
        failingSlot = cls;
        count = 0;
    }
    return ++count >= kFailuresBeforeStandDown;
}

// One line per class that cannot answer, then silence for that class.
void StandDownForClass(std::uintptr_t& slot, std::uintptr_t cls, const char* what) {
    if (slot == cls) return;
    slot = cls;
    Log::Line("ads: %s. Head tracking behaves as though the sights are never raised "
              "while this pawn is possessed, which is hip-fire behaviour through an "
              "aim rather than a crash.", what);
}

// A pointer-sized object property, which is what all three links in the chain
// are. A field the engine reports narrower than a pointer is a different
// property than the one this is reading.
bool ObjectField(std::uintptr_t ustruct, const char* name, std::size_t& outOffset) {
    ue_reflect::FieldInfo f;
    if (!ue_reflect::FindPropertyInChain(ustruct, name, f)) return false;
    if (f.TypeName != "ObjectProperty" || f.Size != sizeof(std::uintptr_t)) return false;
    outOffset = f.Offset;
    return true;
}

// FPVMesh on the pawn's class, and AnimScriptInstance on the mesh's. Both are
// class-level facts, so this runs when the pawn changes and not per frame.
bool ResolveForPawn(std::uintptr_t pawn) {
    const std::uintptr_t pawnClass = ue_reflect::ClassOf(pawn);
    if (!pawnClass) return false;

    std::size_t meshOffset = 0;
    if (!ObjectField(pawnClass, kFpvMeshName, meshOffset)) {
        if (RepeatedFailure(g_failingPawnClass, g_pawnFailures, pawnClass)) {
            ue_reflect::DumpProperties("player pawn", pawnClass);
            WriteOffPawnClass(pawnClass,
                "FPVMesh is not a pointer-sized object property in this pawn's property "
                "chain, so there is no first-person mesh to read the fine-aim flag "
                "through - the table above is what it does carry");
        }
        return false;
    }

    std::uintptr_t mesh = 0;
    if (!ue::SafeReadPtr(pawn + meshOffset, mesh) || !mesh) {
        // The mesh is attached after the pawn exists, so this is a frame to come
        // back on rather than a verdict.
        return false;
    }
    const std::uintptr_t meshClass = ue_reflect::ClassOf(mesh);
    if (!meshClass) return false;

    std::size_t animOffset = 0;
    if (!ObjectField(meshClass, kAnimInstanceName, animOffset)) {
        if (RepeatedFailure(g_failingPawnClass, g_pawnFailures, pawnClass)) {
            ue_reflect::DumpProperties("FPVMesh", meshClass);
            WriteOffPawnClass(pawnClass,
                "AnimScriptInstance is not a pointer-sized object property on this "
                "pawn's FPVMesh, so the animation instance carrying the fine-aim flag "
                "cannot be reached - the table above is what the mesh does carry");
        }
        return false;
    }

    g_pawnFailures = 0;
    g_fpvMeshOffset = meshOffset;
    g_animInstanceOffset = animOffset;
    g_havePawnOffsets = true;
    Log::Line("ads: fine-aim probe resolved (FPVMesh=+0x%zx, AnimScriptInstance=+0x%zx)",
              g_fpvMeshOffset, g_animInstanceOffset);
    return true;
}

// bIsFineAiming on whichever class the live animation instance is.
bool ResolveFlag(std::uintptr_t animClass) {
    ue_reflect::FieldInfo flag;
    if (!ue_reflect::FindPropertyInChain(animClass, kAimFlagName, flag)) {
        if (RepeatedFailure(g_failingAnimClass, g_animFailures, animClass)) {
            ue_reflect::DumpProperties("FPV animation instance", animClass);
            StandDownForClass(g_unsupportedAnimClass, animClass,
                "bIsFineAiming is not in this animation instance's property chain - the "
                "table above is what it does carry");
        }
        return false;
    }
    // A bounds check on the read, and only that. It does NOT tell a native bool
    // from a `uint8 b... : 1` bitfield: FBoolProperty reports ElementSize 1 for
    // both, because a bitfield's element size is that of the byte it is packed
    // into and the bit itself lives in ByteMask, which the build profile's
    // ReflectionLayout does not carry. A bitfield is caught by the value check
    // in IsAimingDownSights instead, where a byte holding its neighbours' bits
    // fails to be a bool.
    if (flag.TypeName != "BoolProperty" || flag.Size != 1) {
        if (RepeatedFailure(g_failingAnimClass, g_animFailures, animClass)) {
            StandDownForClass(g_unsupportedAnimClass, animClass,
                "bIsFineAiming is not a one-byte BoolProperty on this animation "
                "instance");
        }
        return false;
    }
    g_animFailures = 0;
    g_animClass = animClass;
    g_aimFlagOffset = flag.Offset;
    Log::Line("ads: bIsFineAiming at +0x%zx on the first-person animation instance",
              g_aimFlagOffset);
    return true;
}

}  // namespace

bool IsAimingDownSights(std::uintptr_t pawn) {
    if (!ue_reflect::ValidateLayout()) return false;

    if (!pawn) {
        // Between a travel and the next possession there is no pawn, and no
        // sights either. The observed animation class goes with it: Failed() is
        // a claim about the pawn the player is in right now, and left standing
        // it reports adsProbe=FAILED for a class nothing is possessing.
        g_pawnClass = 0;
        g_havePawnOffsets = false;
        g_observedAnimClass = 0;
        return false;
    }
    const std::uintptr_t pawnClass = ue_reflect::ClassOf(pawn);
    if (!pawnClass) return false;
    if (pawnClass != g_pawnClass) {
        g_pawnClass = pawnClass;
        g_havePawnOffsets = false;
        g_animClass = 0;
        g_observedAnimClass = 0;
        // These are raw UClass addresses, and a UClass is a UObject whose slot UE
        // can recycle when a Blueprint is unloaded. Dropping them on a pawn
        // change bounds how long a stale address can go on rejecting whatever
        // later class lands on it.
        g_unsupportedAnimClass = 0;
        g_failingAnimClass = 0;
        g_animFailures = 0;
        // Not the written-off pawn classes: those age out through their own ring
        // rather than on a pawn change, so a class written off once is not
        // re-probed and re-dumped on every fast travel. RepeatedFailure already
        // restarts its own count when the class changes.
    }
    if (PawnClassWrittenOff(pawnClass)) return false;
    if (!g_havePawnOffsets) {
        // Rate limited: DumpProperties walks a whole property chain, and a pawn
        // whose mesh has not been attached yet would otherwise do it every
        // frame it stays that way.
        if (!g_resolveRetry.Due()) return false;
        if (!ResolveForPawn(pawn)) return false;
    }

    std::uintptr_t mesh = 0;
    if (!ue::SafeReadPtr(pawn + g_fpvMeshOffset, mesh) || !mesh) return false;
    std::uintptr_t anim = 0;
    if (!ue::SafeReadPtr(mesh + g_animInstanceOffset, anim) || !anim) return false;

    const std::uintptr_t animClass = ue_reflect::ClassOf(anim);
    if (!animClass) return false;
    g_observedAnimClass = animClass;
    if (animClass == g_unsupportedAnimClass) return false;
    if (animClass != g_animClass) {
        if (!g_flagRetry.Due()) return false;
        if (!ResolveFlag(animClass)) return false;
    }

    std::uint8_t raw = 0;
    if (!cameraunlock::memory::SafeReadU8(anim + g_aimFlagOffset, raw)) return false;
    const unsigned byte = raw;
    if (byte > 1) {
        // Either the offset is not the flag, or the flag is a bitfield sharing
        // its byte with its neighbours. Both are facts about this animation
        // CLASS, so the stand-down is scoped to it - but only after the class has
        // said so several frames running, because a single torn read while a
        // graph re-initialises must cost a frame rather than the session.
        if (!g_flagRetry.Due()) return false;
        if (RepeatedFailure(g_failingAnimClass, g_animFailures, animClass)) {
            StandDownForClass(g_unsupportedAnimClass, animClass,
                "bIsFineAiming holds a value that is not a bool, so the byte at the "
                "offset the engine reported is either a different property or a "
                "bitfield sharing its byte with its neighbours");
        }
        return false;
    }
    // A good read is the class agreeing with itself, so it clears the count the
    // same way a successful resolve does. Without this the failures accumulate
    // across a whole session rather than having to arrive consecutively, and
    // four scattered transients write off a healthy class.
    g_animFailures = 0;
    return byte == 1;
}

bool Failed() {
    // Either half. Three of the five stand-downs are on the ANIMATION class, and
    // a heartbeat that reported only the pawn half would print adsProbe=ok for a
    // whole session in which the sights were never once detected.
    return (g_pawnClass != 0 && PawnClassWrittenOff(g_pawnClass)) ||
           (g_observedAnimClass != 0 && g_observedAnimClass == g_unsupportedAnimClass);
}

}  // namespace tow_ht::ads_state

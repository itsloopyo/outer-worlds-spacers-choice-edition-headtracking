// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>
#include <string>

// Calling into the game's script VM, and reading the object graph around it.
//
// Three features need this - the reticle mover, the aim trace and the lean
// sweep - and all three do it the same way: resolve UObject::ProcessEvent from
// the active build profile once, then dispatch a UFUNCTION through it behind a
// fault guard. Every caller must already be on the game thread.
namespace tow_ht::ue_vm {

// Resolve UObject::ProcessEvent from the active build profile, once. False when
// the profile carries no RVA for it, in which case nothing else here can run.
bool Ready();

// The RVA Ready() resolved, for the caller that wants to say so in the log.
std::uintptr_t ProcessEventRva();

// Dispatch a UFUNCTION. Returns false if the call faulted - the object stopped
// being what the caller thought it was between its liveness test and here - so
// the caller can drop it and go looking for its replacement rather than pushing
// into a dead object forever.
bool Dispatch(void* self, void* function, void* params);

// Whether `obj` is still registered in GUObjectArray, which is the authority on
// whether an object exists. Freed UObject memory keeps its old class pointer for
// as long as the allocator leaves it alone, so a class-pointer test on its own
// reports a destroyed object as live indefinitely. The array item at the
// object's own InternalIndex points back at it only while it is registered.
bool IsRegistered(std::uintptr_t obj);

// The chain of outer names above `obj`, at most `depth` links, each one
// prefixed with `separator`. Stops early at the first outer that will not read.
std::string OuterChain(std::uintptr_t obj, int depth, const char* separator);

// Rate limit for a resolve that has not succeeded yet.
//
// Every lookup here goes through FindLiveObject, which walks the whole
// GUObjectArray and builds a std::string for each live object it passes -
// hundreds of thousands of them in a shipping build. A feature whose
// objects are not visible yet must therefore NOT retry on every frame, or the
// render thread stalls for as long as they stay invisible.
//
// One instance per feature rather than one shared clock, so a probe that has
// just retried does not block another feature's first attempt. The objects
// appear at engine init, and a quarter second is invisible against a level
// load.
class ResolveRetry {
public:
    // True at most once per interval. On the frames in between an unresolved
    // feature costs one clock read.
    bool Due();

private:
    static constexpr std::uint64_t kIntervalMs = 250;
    std::uint64_t m_lastAttemptMs = 0;
};

}  // namespace tow_ht::ue_vm

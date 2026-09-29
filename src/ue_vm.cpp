// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "ue_vm.h"

#include <windows.h>

#include "builds/build_registry.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace tow_ht::ue_vm {

namespace {

namespace ue = ::cameraunlock::unreal;

using ProcessEvent_t = void(__fastcall*)(void* self, void* function, void* params);
ProcessEvent_t g_processEvent = nullptr;

// Non-unwinding, so __try is legal here while callers hold objects with
// destructors.
bool Invoke(void* self, void* function, void* params) {
    __try {
        g_processEvent(self, function, params);
        return true;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
                    ? EXCEPTION_EXECUTE_HANDLER
                    : EXCEPTION_CONTINUE_SEARCH) {
        // An access violation is the one this answers for, and callers read a
        // false as "the object stopped being what I thought it was" and drop it.
        // A stack overflow, an in-page error, or a C++ exception the engine
        // raised from inside the call all mean something else entirely and leave
        // the VM mid-call, so they go on up rather than being relabelled as a
        // dead pointer.
        return false;
    }
}

}  // namespace

bool Ready() {
    if (g_processEvent) return true;
    const std::uintptr_t rva = Offsets().kProcessEventRva;
    if (rva == 0) return false;
    g_processEvent = reinterpret_cast<ProcessEvent_t>(ue::ModuleBase() + rva);
    return true;
}

std::uintptr_t ProcessEventRva() { return Offsets().kProcessEventRva; }

bool Dispatch(void* self, void* function, void* params) {
    if (!Ready()) return false;
    return Invoke(self, function, params);
}

bool ResolveRetry::Due() {
    const std::uint64_t now = GetTickCount64();
    // Unsigned wrap is not a concern: GetTickCount64 needs 585 million years
    // to overflow, and m_lastAttemptMs starts at zero, so the first call is
    // always due.
    if (now - m_lastAttemptMs < kIntervalMs) return false;
    m_lastAttemptMs = now;
    return true;
}

bool IsRegistered(std::uintptr_t obj) {
    // UObjectBase packs InternalIndex immediately before ClassPrivate.
    std::uint32_t index = 0;
    if (!ue::SafeReadU32(obj + Offsets().UObjectGlobals.kClassPrivate - 4, index)) return false;
    return index < ObjectCount() && ObjectAt(index) == obj;
}

std::uint32_t ObjectCount() {
    const auto& g = Offsets().UObjectGlobals;
    // Both are divisors in ObjectAt, so a profile missing either has no array.
    if (g.kChunkNumElems == 0 || g.kFUObjectItemSize == 0) return 0;
    std::uint32_t num = 0;
    if (!ue::SafeReadU32(ue::ModuleBase() + g.kObjObjects + g.kObjObjects_Num, num)) return 0;
    return num;
}

std::uintptr_t ObjectAt(std::uint32_t index) {
    const auto& g = Offsets().UObjectGlobals;
    std::uintptr_t chunks = 0;
    if (!ue::SafeReadPtr(ue::ModuleBase() + g.kObjObjects, chunks) || !chunks) return 0;
    std::uintptr_t chunk = 0;
    if (!ue::SafeReadPtr(chunks + (static_cast<std::uintptr_t>(index / g.kChunkNumElems) * 8),
                         chunk) || !chunk)
        return 0;
    std::uintptr_t obj = 0;
    if (!ue::SafeReadPtr(chunk + static_cast<std::uintptr_t>(index % g.kChunkNumElems)
                             * g.kFUObjectItemSize, obj))
        return 0;
    return obj;
}

std::string OuterChain(std::uintptr_t obj, int depth, const char* separator) {
    std::string out;
    std::uintptr_t cur = obj;
    for (int i = 0; i < depth; ++i) {
        std::uintptr_t outer = 0;
        if (!ue::SafeReadPtr(cur + Offsets().UObjectGlobals.kOuterPrivate, outer) || !outer)
            break;
        out += separator;
        out += ue::ObjectName(outer);
        cur = outer;
    }
    return out;
}

}  // namespace tow_ht::ue_vm

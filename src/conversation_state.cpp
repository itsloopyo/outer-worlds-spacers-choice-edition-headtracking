// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "conversation_state.h"

#include <cstdint>
#include <string>
#include <unordered_map>

#include <windows.h>

#include "builds/build_registry.h"
#include "logging.h"
#include "ue_reflect.h"
#include "ue_vm.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace tow_ht::conversation_state {

namespace {

namespace ue = ::cameraunlock::unreal;

// The dialogue UI's Blueprint class. It is built the first time the player
// talks to anyone and freed by a later garbage collection, so it cannot be
// looked up once at startup.
constexpr const char* kWidgetClassName = "ConversationWidget_BP_C";

// The object table is searched a slice at a time while the cursor is up and no
// widget answering yes is held. One pass over the whole table (~90k objects)
// measured 5.2ms on the game thread, and run in one go that is a hitch four
// times a second for as long as the player sits in the inventory or the pause
// menu. A slice is under a fifth of the table. Slices are at least
// kSliceSpacingMs apart, so the hook's several entries inside one frame do not
// stack them, and a pass starts at most every kPassIntervalMs, which bounds how
// late tracking starts after a conversation opens.
constexpr std::uint32_t kSliceObjects = 16384;
constexpr std::uint64_t kSliceSpacingMs = 8;
constexpr std::uint64_t kPassIntervalMs = 250;

// The class's FName comparison index, learned by the first walk that meets it.
// Until then every distinct class name is resolved to a string once and the
// answer kept, so a walk resolves each name at most once for the whole session
// rather than once per object.
std::uint32_t g_classNameId = 0;
std::unordered_map<std::uint32_t, bool> g_nameVerdicts;

std::size_t g_inputOffset = 0;
bool g_inputResolved = false;
bool g_unavailable = false;

std::uintptr_t g_held = 0;
std::uintptr_t g_heldCls = 0;

// The next GUObjectArray slot of the pass in progress; 0 when none is.
std::uint32_t g_cursor = 0;
std::uint64_t g_passStartMs = 0;
std::uint64_t g_lastSliceMs = 0;

bool IsWidgetClass(std::uintptr_t cls) {
    std::uint32_t id = 0;
    if (!ue::SafeReadU32(cls + Offsets().UObjectGlobals.kNamePrivate, id)) return false;
    if (g_classNameId != 0) return id == g_classNameId;
    const auto it = g_nameVerdicts.find(id);
    if (it != g_nameVerdicts.end()) return it->second;
    const bool match = ue::ResolveFName(id) == kWidgetClassName;
    if (match) {
        g_classNameId = id;
        g_nameVerdicts.clear();
    } else {
        g_nameVerdicts.emplace(id, false);
    }
    return match;
}

// UUserWidget::InputComponent, resolved by name off the first widget found.
bool ResolveInputOffset(std::uintptr_t cls) {
    if (g_inputResolved) return true;
    ue_reflect::FieldInfo f;
    if (!ue_reflect::FindPropertyInChain(cls, "InputComponent", f)
        || f.TypeName != "ObjectProperty" || f.Size != sizeof(std::uintptr_t)) {
        g_unavailable = true;
        Log::Line("conversation: %s has no pointer-sized InputComponent property, so a "
                  "conversation cannot be told from a menu - head tracking stands down "
                  "in dialogue as it does in every other menu",
            kWidgetClassName);
        return false;
    }
    g_inputOffset = f.Offset;
    g_inputResolved = true;
    Log::Line("conversation: %s InputComponent at +0x%zx", kWidgetClassName, g_inputOffset);
    return true;
}

bool TakingInput(std::uintptr_t widget) {
    std::uintptr_t input = 0;
    return ue::SafeReadPtr(widget + g_inputOffset, input) && input != 0;
}

bool HeldIsLive() {
    std::uintptr_t cls = 0;
    return ue::SafeReadPtr(g_held + Offsets().UObjectGlobals.kClassPrivate, cls)
        && cls == g_heldCls && ue_vm::IsRegistered(g_held);
}

// `obj` when it is the dialogue widget taking the player's responses, else 0.
std::uintptr_t Match(std::uintptr_t obj) {
    std::uintptr_t cls = 0;
    if (!ue::SafeReadPtr(obj + Offsets().UObjectGlobals.kClassPrivate, cls) || !cls) return 0;
    if (!IsWidgetClass(cls) || !ResolveInputOffset(cls) || !TakingInput(obj)) return 0;
    g_heldCls = cls;
    return obj;
}

// The next slice of the pass in progress. The pass ends, and the cursor goes
// back to 0, at the end of the table, on a find, or when ResolveInputOffset has
// written the feature off.
std::uintptr_t WalkSlice() {
    const std::uint32_t count = ue_vm::ObjectCount();
    std::uint32_t i = g_cursor;
    const std::uint32_t end =
        (i < count && count - i > kSliceObjects) ? i + kSliceObjects : count;
    std::uintptr_t found = 0;
    for (; i < end && !found && !g_unavailable; ++i) {
        const std::uintptr_t obj = ue_vm::ObjectAt(i);
        if (obj) found = Match(obj);
    }
    g_cursor = (found || g_unavailable || i >= count) ? 0 : i;
    return found;
}

}  // namespace

bool Active() {
    if (g_unavailable) return false;
    // FindPropertyInChain reads through the profile's reflection layout, which
    // means nothing until it has been proved against this build.
    if (!ue_reflect::ValidateLayout()) return false;

    if (g_held != 0) {
        if (HeldIsLive() && TakingInput(g_held)) return true;
        g_held = 0;
    }

    const std::uint64_t now = GetTickCount64();
    if (g_cursor == 0) {
        if (g_passStartMs != 0 && now - g_passStartMs < kPassIntervalMs) return false;
        g_passStartMs = now;
    } else if (now - g_lastSliceMs < kSliceSpacingMs) {
        return false;
    }
    g_lastSliceMs = now;

    LARGE_INTEGER freq{}, t0{}, t1{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    g_held = WalkSlice();
    QueryPerformanceCounter(&t1);

    static bool s_timed = false;
    if (!s_timed) {
        s_timed = true;
        Log::Line("conversation: an object-table slice of up to %u objects took %.2fms",
            kSliceObjects,
            freq.QuadPart ? static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0
                                / static_cast<double>(freq.QuadPart)
                          : 0.0);
    }
    return g_held != 0;
}

}  // namespace tow_ht::conversation_state

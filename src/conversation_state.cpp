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

// How often the object table is walked while the cursor is up and no widget
// answering yes is held. A walk is a pass over ~90k objects on the game thread;
// it only runs in a menu or in the first moments of a dialogue, and this bounds
// how late tracking starts after a conversation opens.
constexpr std::uint64_t kWalkIntervalMs = 250;

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
std::uint64_t g_lastWalkMs = 0;

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

std::uintptr_t Walk() {
    const std::size_t classOff = Offsets().UObjectGlobals.kClassPrivate;
    std::uintptr_t found = 0;
    ue::ForEachUObject([&](std::uintptr_t obj) -> bool {
        std::uintptr_t cls = 0;
        if (!ue::SafeReadPtr(obj + classOff, cls) || !cls) return false;
        if (!IsWidgetClass(cls)) return false;
        if (!ResolveInputOffset(cls)) return true;
        if (!TakingInput(obj)) return false;
        found = obj;
        g_heldCls = cls;
        return true;
    });
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
    if (g_lastWalkMs != 0 && now - g_lastWalkMs < kWalkIntervalMs) return false;
    g_lastWalkMs = now;

    LARGE_INTEGER freq{}, t0{}, t1{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    g_held = Walk();
    QueryPerformanceCounter(&t1);

    static bool s_timed = false;
    if (!s_timed) {
        s_timed = true;
        Log::Line("conversation: object-table walk took %.1fms",
            freq.QuadPart ? static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0
                                / static_cast<double>(freq.QuadPart)
                          : 0.0);
    }
    return g_held != 0;
}

}  // namespace tow_ht::conversation_state

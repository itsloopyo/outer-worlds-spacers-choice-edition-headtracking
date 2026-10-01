// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "weapon_view.h"

#include <windows.h>
#include <array>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

#include "builds/build_registry.h"
#include "cameraunlock/hooks/hook_manager.h"
#include "cameraunlock/unreal/ue_runtime.h"
#include "logging.h"
#include "ue_reflect.h"
#include "ue_vm.h"
#include "weapon_shader.h"

namespace tow_ht::weapon_view {
namespace {
namespace ue = cameraunlock::unreal;
using SceneFn = void*(__fastcall*)(void*, void*, void*, void*, void*, void*, int);
SceneFn g_scene = nullptr;
thread_local bool g_inScene = false;
thread_local bool g_updated = false;

struct Owned {
    std::size_t offset;
    std::vector<float> original;
};
struct State {
    std::uintptr_t setter = 0;
    ue_reflect::FieldInfo index, value;
    std::size_t frameSize = 0;
    ue_vm::ResolveRetry retry;
    std::map<std::uintptr_t, Owned> components;
    std::uintptr_t root = 0;
    ue::FQuat4d previous{};
    bool failed = false;
};
State& Data() {
    // Hooks remain installed while the engine tears down after CRT destruction.
    static auto* state = new State;
    return *state;
}

std::uintptr_t Pointer(std::uintptr_t at) {
    std::uintptr_t value = 0;
    if (!ue::SafeReadPtr(at, value)) throw std::runtime_error("weapon component pointer is unreadable");
    return value;
}

ue_reflect::FieldInfo Field(std::uintptr_t object, const char* name, std::size_t size) {
    ue_reflect::FieldInfo field;
    const auto cls = ue_reflect::ClassOf(object);
    if (!ue_reflect::FindPropertyInChain(cls, name, field) ||
        !ue_reflect::FieldFits(field, size, ue_reflect::StructSize(cls)))
        throw std::runtime_error(std::string("invalid weapon component field: ") + name);
    return field;
}

struct Array { std::uintptr_t data; std::uint32_t count; };
Array ReadArray(std::uintptr_t at, std::uint32_t limit) {
    Array result{Pointer(at), 0};
    std::uint32_t capacity = 0;
    if (!ue::SafeReadU32(at + 8, result.count) || !ue::SafeReadU32(at + 12, capacity) ||
        result.count > limit || result.count > capacity || (result.count && !result.data))
        throw std::runtime_error("invalid weapon component array");
    return result;
}

bool Owns(std::uintptr_t object, const Owned& owned) {
    if (!ue_vm::IsRegistered(object) ||
        !ue_reflect::ClassDerivesFrom(ue_reflect::ClassOf(object), "PrimitiveComponent")) return false;
    const auto array = ReadArray(object + owned.offset, 36);
    std::uint32_t marker = 0;
    return array.count == 36 && ue::SafeReadU32(array.data + 35 * 4, marker) &&
           marker == weapon_shader::kPrimitiveMarker;
}

void Set(std::uintptr_t object, int index, float value) {
    auto& state = Data();
    std::vector<std::uint8_t> params(state.frameSize);
    std::memcpy(params.data() + state.index.Offset, &index, sizeof(index));
    std::memcpy(params.data() + state.value.Offset, &value, sizeof(value));
    if (!ue_vm::Dispatch(reinterpret_cast<void*>(object), reinterpret_cast<void*>(state.setter), params.data()))
        throw std::runtime_error("SetCustomPrimitiveDataFloat dispatch failed");
}

bool SetCount(std::uintptr_t at, std::uint32_t count) {
    __try { *reinterpret_cast<std::uint32_t*>(at) = count; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void Restore(std::uintptr_t object, const Owned& owned) {
    if (!Owns(object, owned)) return;
    for (int i = 0; i < 36; ++i)
        Set(object, i, static_cast<std::size_t>(i) < owned.original.size() ? owned.original[i] : 0.f);
    // The setter schedules a render-state rebuild; restore the original array length before that rebuild.
    if (!SetCount(object + owned.offset + 8, static_cast<std::uint32_t>(owned.original.size())))
        throw std::runtime_error("cannot restore weapon custom data length");
}

void Collect(std::uintptr_t object, std::set<std::uintptr_t>& visited,
             std::set<std::uintptr_t>& primitives) {
    if (!object || !visited.insert(object).second) return;
    if (visited.size() > 512 || !ue_vm::IsRegistered(object) ||
        !ue_reflect::ClassDerivesFrom(ue_reflect::ClassOf(object), "SceneComponent"))
        throw std::runtime_error("invalid first-person component hierarchy");
    if (ue_reflect::ClassDerivesFrom(ue_reflect::ClassOf(object), "PrimitiveComponent")) primitives.insert(object);
    const auto children = ReadArray(object + Field(object, "AttachChildren", 16).Offset, 512);
    for (std::uint32_t i = 0; i < children.count; ++i)
        Collect(Pointer(children.data + i * sizeof(void*)), visited, primitives);
}

bool Resolve() {
    auto& state = Data();
    if (state.setter) return true;
    if (!state.retry.Due() || !ue_reflect::ValidateLayout() || !ue_vm::Ready()) return false;
    const auto setter = ue::FindLiveObject("Function", "SetCustomPrimitiveDataFloat", "PrimitiveComponent");
    if (!setter) return false;
    state.frameSize = ue_reflect::StructSize(setter);
    if (state.frameSize > 64 ||
        !ue_reflect::FindPropertyInChain(setter, "DataIndex", state.index) ||
        !ue_reflect::FindPropertyInChain(setter, "Value", state.value) ||
        state.index.TypeName != "IntProperty" || state.value.TypeName != "FloatProperty" ||
        !ue_reflect::FieldFits(state.index, 4, state.frameSize) ||
        !ue_reflect::FieldFits(state.value, 4, state.frameSize))
        throw std::runtime_error("invalid SetCustomPrimitiveDataFloat parameter layout");
    state.setter = setter;
    Log::Line("weapon: custom primitive data setter validated");
    return true;
}

void Apply(std::uintptr_t controller, const ue::FRotator& clean) {
    auto& state = Data();
    const auto pawn = Pointer(controller + Field(controller, "Pawn", sizeof(void*)).Offset);
    ue_reflect::FieldInfo fpv;
    const auto root = pawn && ue_reflect::FindPropertyInChain(ue_reflect::ClassOf(pawn), "FPVMesh", fpv)
        ? Pointer(pawn + Field(pawn, "FPVMesh", sizeof(void*)).Offset) : 0;
    std::set<std::uintptr_t> visited, primitives;
    Collect(root, visited, primitives);
    for (auto it = state.components.begin(); it != state.components.end();) {
        if (!primitives.count(it->first)) {
            Restore(it->first, it->second);
            it = state.components.erase(it);
        } else if (!Owns(it->first, it->second)) {
            it = state.components.erase(it);
        } else ++it;
    }
    const auto current = ue::QuatFromEulerDeg(clean.Pitch, clean.Yaw, clean.Roll);
    if (root != state.root) state.previous = current;
    state.root = root;
    const auto right = ue::QuatRotateVec(current, {0,1,0});
    const auto up = ue::QuatRotateVec(current, {0,0,1});
    const auto forward = ue::QuatRotateVec(current, {1,0,0});
    const auto pr = ue::QuatRotateVec(state.previous, {0,1,0});
    const auto pu = ue::QuatRotateVec(state.previous, {0,0,1});
    const auto pf = ue::QuatRotateVec(state.previous, {1,0,0});
    std::array<float, 36> values{
        float(right.X),float(up.X),float(forward.X),0,
        float(right.Y),float(up.Y),float(forward.Y),0,
        float(right.Z),float(up.Z),float(forward.Z),0,
        float(right.X),float(right.Y),float(right.Z),0,
        float(up.X),float(up.Y),float(up.Z),0,
        float(forward.X),float(forward.Y),float(forward.Z),0,
        float(pr.X),float(pu.X),float(pf.X),0,
        float(pr.Y),float(pu.Y),float(pf.Y),0,
        float(pr.Z),float(pu.Z),float(pf.Z),0};
    std::memcpy(&values[35], &weapon_shader::kPrimitiveMarker, 4);
    for (const auto object : primitives) {
        if (!state.components.count(object)) {
            const auto internal = Field(object, "CustomPrimitiveDataInternal", 16).Offset;
            const auto defaults = Field(object, "CustomPrimitiveData", 16).Offset;
            Owned owned{internal, {}};
            for (const auto offset : {internal, defaults}) {
                const auto array = ReadArray(object + offset, 36);
                for (std::uint32_t i = 0; i < array.count; ++i) {
                    float value = 0;
                    if (!ue::SafeReadFloat(array.data + i * 4, value) || value != 0.f)
                        throw std::runtime_error("first-person component already uses custom primitive data");
                }
                if (offset == internal) owned.original.resize(array.count, 0.f);
            }
            state.components.emplace(object, std::move(owned));
            Log::Line("weapon: clean aim bound to first-person %s %p", ue::ClassName(object).c_str(), reinterpret_cast<void*>(object));
        }
        for (int i = 0; i < 36; ++i) Set(object, i, values[i]);
    }
    state.previous = current;
}

void* __fastcall Scene(void* a, void* b, void* c, void* d, void* e, void* f, int g) {
    const bool previousScene = g_inScene, previousUpdate = g_updated;
    g_inScene = true;
    g_updated = false;
    auto* result = g_scene(a, b, c, d, e, f, g);
    g_inScene = previousScene;
    g_updated = previousUpdate;
    return result;
}
}

bool Update(std::uintptr_t controller, const ue::FRotator& clean) {
    auto& state = Data();
    if (state.failed) return false;
    if (!g_inScene || g_updated) return true;
    try {
        if (!Resolve()) return false;
        Apply(controller, clean);
        g_updated = true;
        return true;
    } catch (const std::exception& error) {
        Log::Line("FATAL: weapon view correction failed: %s; tracking disabled", error.what());
        state.failed = true;
        return false;
    }
}

bool Install() {
    auto& manager = cameraunlock::hooks::HookManager::Instance();
    auto* target = reinterpret_cast<void*>(ue::ModuleBase() + builds::SceneViewRva());
    auto status = manager.CreateHook(target, reinterpret_cast<void*>(&Scene), reinterpret_cast<void**>(&g_scene));
    if (status == cameraunlock::hooks::HookStatus::Ok) status = manager.EnableHook(target);
    if (status != cameraunlock::hooks::HookStatus::Ok) {
        Log::Line("FATAL: scene-view hook failed: %s", cameraunlock::hooks::HookStatusToString(status));
        return false;
    }
    return true;
}
}

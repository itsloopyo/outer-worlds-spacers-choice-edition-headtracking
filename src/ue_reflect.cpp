// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "ue_reflect.h"

#include <cstdint>

#include <vector>

#include "builds/build_registry.h"
#include "kismet_frame.h"
#include "logging.h"
#include "ue_vm.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace tow_ht::ue_reflect {

namespace {

namespace ue = ::cameraunlock::unreal;

// A property chain longer than this is not a property chain. UE's largest
// reflected structs are a few hundred fields; anything past this means the
// Next offset is wrong and the walk is following heap noise, so it stops
// rather than reading until it faults.
constexpr int kMaxChain = 4096;

std::string FieldName(std::uintptr_t field) {
    std::uint32_t id = 0;
    if (!ue::SafeReadU32(field + Offsets().Reflection.kFField_NamePrivate, id)) return {};
    return ue::ResolveFName(id);
}

std::string FieldTypeName(std::uintptr_t field) {
    std::uintptr_t cls = 0;
    if (!ue::SafeReadPtr(field + Offsets().Reflection.kFField_ClassPrivate, cls) || !cls)
        return {};
    std::uint32_t id = 0;
    if (!ue::SafeReadU32(cls + Offsets().Reflection.kFFieldClass_Name, id)) return {};
    return ue::ResolveFName(id);
}

// Everything recorded about one field: two FName resolves, so two strings off
// the heap, plus three dword reads. Filled for a field that has already MATCHED
// rather than for every field walked past - see FindProperty.
FieldInfo ReadField(std::uintptr_t field) {
    const auto& r = Offsets().Reflection;
    FieldInfo info;
    info.Name     = FieldName(field);
    info.TypeName = FieldTypeName(field);

    std::uint32_t offset = 0, elementSize = 0, arrayDim = 0;
    if (ue::SafeReadU32(field + r.kFProperty_Offset, offset) &&
        ue::SafeReadU32(field + r.kFProperty_ElementSize, elementSize) &&
        ue::SafeReadU32(field + r.kFProperty_ArrayDim, arrayDim)) {
        info.Offset = offset;
        info.Size   = static_cast<std::size_t>(elementSize) * (arrayDim ? arrayDim : 1);
    }
    return info;
}

// The FField chain hanging off a UStruct, in declaration order. `visit(field)`
// returns true to stop early.
template <typename Fn>
void ForEachField(std::uintptr_t ustruct, Fn&& visit) {
    if (!ustruct) return;
    const auto& r = Offsets().Reflection;

    std::uintptr_t field = 0;
    if (!ue::SafeReadPtr(ustruct + r.kUStruct_ChildProperties, field)) return;

    for (int i = 0; field && i < kMaxChain; ++i) {
        if (visit(field)) return;
        std::uintptr_t next = 0;
        if (!ue::SafeReadPtr(field + r.kFField_Next, next)) return;
        field = next;
    }
}

// Every reflected property of a UStruct, in declaration order. Empty when the
// struct pointer or the reflection layout does not read.
std::vector<FieldInfo> Properties(std::uintptr_t ustruct) {
    std::vector<FieldInfo> out;
    ForEachField(ustruct, [&out](std::uintptr_t field) {
        out.push_back(ReadField(field));
        return false;
    });
    return out;
}

// One named property of one struct. Walks the chain and reads only each field's
// NAME until one matches, rather than materialising the whole property table
// and searching it. The table is two heap strings per field, and the callers ask
// repeatedly against the same struct - ResolveAll once per name it wants,
// FindPropertyInChain once per class above the leaf - so building it per lookup
// was the whole cost of resolving a ten-parameter UFunction.
bool FindProperty(std::uintptr_t ustruct, const char* name, FieldInfo& out) {
    bool found = false;
    ForEachField(ustruct, [&](std::uintptr_t field) {
        if (!ue::EqualsCI(FieldName(field), name)) return false;
        out = ReadField(field);
        found = true;
        return true;
    });
    return found;
}

ue_vm::ResolveRetry g_validateRetry;
bool g_validated = false;
bool g_rejected = false;
int  g_validateAttempts = 0;

}  // namespace

std::size_t StructSize(std::uintptr_t ustruct) {
    if (!ustruct) return 0;
    std::uint32_t size = 0;
    if (!ue::SafeReadU32(ustruct + Offsets().Reflection.kUStruct_PropertiesSize, size))
        return 0;
    return size;
}

std::uintptr_t ClassOf(std::uintptr_t obj) {
    std::uintptr_t cls = 0;
    if (!obj || !ue::SafeReadPtr(obj + ue::Layout().kClassPrivate, cls)) return 0;
    return cls;
}

bool ClassDerivesFrom(std::uintptr_t cls, const char* baseName) {
    const auto& r = Offsets().Reflection;
    std::uintptr_t cur = cls;
    // Bounded for the same reason FindPropertyInChain is: a wrong SuperStruct
    // offset reads a pointer-shaped field that can point back into the graph.
    for (int depth = 0; cur && depth < 32; ++depth) {
        if (ue::EqualsCI(ue::ObjectName(cur), baseName)) return true;
        std::uintptr_t super = 0;
        if (!ue::SafeReadPtr(cur + r.kUStruct_SuperStruct, super)) return false;
        cur = super;
    }
    return false;
}

bool FindPropertyInChain(std::uintptr_t ustruct, const char* name, FieldInfo& out) {
    const auto& r = Offsets().Reflection;
    std::uintptr_t cur = ustruct;
    // Bounded rather than "until SuperStruct is null": a wrong kUStruct_SuperStruct
    // reads a pointer-shaped field that happens to point back into the object
    // graph, and the walk then never terminates.
    for (int depth = 0; cur && depth < 32; ++depth) {
        if (FindProperty(cur, name, out)) return true;
        std::uintptr_t super = 0;
        if (!ue::SafeReadPtr(cur + r.kUStruct_SuperStruct, super)) return false;
        cur = super;
    }
    return false;
}

void DumpProperties(const char* label, std::uintptr_t ustruct) {
    const std::size_t size = StructSize(ustruct);
    Log::Line("reflect: %s @0x%llx PropertiesSize=%zu", label,
        static_cast<unsigned long long>(ustruct), size);
    for (const FieldInfo& f : Properties(ustruct)) {
        Log::Line("reflect:   +0x%03zx size %-4zu %-18s %s",
            f.Offset, f.Size, f.TypeName.c_str(), f.Name.c_str());
    }
}

bool ResolveAll(const char* label, std::uintptr_t ustruct,
                const std::vector<std::string>& names,
                std::vector<FieldInfo>& out) {
    out.clear();
    const std::size_t size = StructSize(ustruct);
    // A reflected struct with no size, or one bigger than any real UE struct,
    // says PropertiesSize is not where the profile claims. Everything below
    // would then be measured against nonsense.
    if (size == 0 || size > 0x10000) {
        Log::Line("reflect: %s has PropertiesSize=%zu, which is not a struct size - "
                  "the ReflectionLayout in this build profile does not fit this game "
                  "build", label, size);
        DumpProperties(label, ustruct);
        return false;
    }

    bool ok = true;
    for (const std::string& want : names) {
        FieldInfo f;
        if (!FindProperty(ustruct, want.c_str(), f)) {
            Log::Line("reflect: %s has no property named %s", label, want.c_str());
            ok = false;
            break;
        }
        if (f.Size == 0 || f.Offset + f.Size > size) {
            Log::Line("reflect: %s.%s lands at +0x%zx size %zu, outside its own "
                      "PropertiesSize %zu", label, want.c_str(), f.Offset, f.Size, size);
            ok = false;
            break;
        }
        out.push_back(f);
    }

    if (!ok) {
        out.clear();
        DumpProperties(label, ustruct);
    }
    return ok;
}

bool ValidateLayout() {
    if (g_validated) return true;
    if (g_rejected) return false;
    if (!g_validateRetry.Due()) return false;

    const std::uintptr_t mvi =
        ue::FindLiveObject("ScriptStruct", "MinimalViewInfo", nullptr);
    if (!mvi) {
        // FindLiveObject walks the whole GUObjectArray and builds a std::string
        // for every live object it passes, and on a miss it visits all of them.
        // This gate stands every reflection-backed feature down, so on a build
        // where the struct never appears it is the ONLY thing running - four
        // full walks a second on the render thread, for the session, with
        // nothing in the log naming the cause. The traces bound the same lookup
        // with the same constant; this one was the exception.
        if (++g_validateAttempts >= kismet_frame::kMaxLookupAttempts) {
            g_rejected = true;
            Log::Line("reflect: UScriptStruct MinimalViewInfo never reached the "
                      "object table. The reflection layout cannot be proved, so the "
                      "reticle, the aim marker, the lean clamp and the zoom "
                      "compensation all stay down for this session. The view still "
                      "tracks your head.");
        }
        return false;   // not reached the object table yet
    }

    std::vector<FieldInfo> f;
    if (!ResolveAll("MinimalViewInfo", mvi,
                    {"Location", "Rotation", "FOV", "AspectRatio"}, f)) {
        // The header of the UStruct itself, so the numbers that moved can be
        // read off rather than guessed at. FMinimalViewInfo is the one struct
        // whose layout is already known from the render caller, so its
        // PropertiesSize and ChildProperties are identifiable by eye.
        Log::Line("reflect: UScriptStruct MinimalViewInfo header dump:");
        for (std::size_t off = 0; off < 0x90; off += 8) {
            std::uintptr_t q = 0;
            std::uint32_t lo = 0, hi = 0;
            if (!ue::SafeReadPtr(mvi + off, q)) break;
            ue::SafeReadU32(mvi + off, lo);
            ue::SafeReadU32(mvi + off + 4, hi);
            Log::Line("reflect:   +0x%02zx  %016llx  u32=%u,%u%s", off,
                static_cast<unsigned long long>(q), lo, hi,
                ue::LooksLikePointer(q) ? "  <ptr>" : "");
        }
        g_rejected = true;
        Log::Line("reflect: the ReflectionLayout in this build profile does not read "
                  "MinimalViewInfo, so every feature that walks the engine's "
                  "reflection data stands down - the crosshair stays where the game "
                  "puts it and the lean runs unclamped. The camera hook itself is "
                  "unaffected.");
        return false;
    }

    const auto& known = Offsets().MinimalViewInfoLayout;
    const bool ok = f[0].Offset == 0 &&
                    f[1].Offset == known.kRotationStride &&
                    f[2].Offset == known.kFovOffset &&
                    f[3].Offset == known.kAspectRatioOffset;
    Log::Line("reflect: MinimalViewInfo Location=+0x%zx Rotation=+0x%zx FOV=+0x%zx "
              "AspectRatio=+0x%zx (profile says 0x0 / 0x%zx / 0x%zx / 0x%zx) - %s",
        f[0].Offset, f[1].Offset, f[2].Offset, f[3].Offset,
        known.kRotationStride, known.kFovOffset, known.kAspectRatioOffset,
        ok ? "match" : "MISMATCH");
    if (!ok) {
        g_rejected = true;
        Log::Line("reflect: the reflection data disagrees with the offsets this "
                  "build profile carries, so the ReflectionLayout is being walked "
                  "through the wrong pointers. Everything reflection-backed stands "
                  "down.");
        return false;
    }
    g_validated = true;
    return true;
}

}  // namespace tow_ht::ue_reflect

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Read the engine's own reflection data for a UStruct - a UFunction's parameter
// list, or a UScriptStruct's fields.
//
// Why this exists rather than a table of hardcoded struct offsets: the mod calls
// UKismetSystemLibrary::LineTraceSingle through the script VM and reads
// FHitResult::ImpactPoint out of the result. Both layouts are long, both contain
// LWC doubles whose padding moved between engine versions, and getting one field
// wrong produces an aim point that is plausible-looking garbage - the exact
// failure the reticle doctrine says costs days. The engine already stores the
// answer, keyed by name, in data this mod can read. So it asks.
//
// The only layout constants in the mod are the ones needed to read THAT data,
// and they live in the build profile's ReflectionLayout. Every one of them is
// checked before use: a UFunction whose parameter names do not resolve to
// plausible ascending offsets inside its own PropertiesSize is reported and the
// feature that needed it stands down, rather than writing into a buffer at
// offsets read out of noise.
namespace tow_ht::ue_reflect {

struct FieldInfo {
    std::string    Name;
    std::string    TypeName;   // FFieldClass name: "StructProperty", "BoolProperty", ...
    std::size_t    Offset = 0;
    std::size_t    Size = 0;   // ElementSize * ArrayDim
};

// True when a fixed-size read or write of `bytes` at `f.Offset` stays inside a
// frame of `frame_size` AND the engine's own size for the field is at least
// that wide.
//
// ResolveAll answers a different question: it proves a named field sits inside
// its own struct at the size THE ENGINE reports. That says nothing about the
// fixed-size type a caller is about to memcpy into the slot. A parameter frame
// is written into a stack buffer at these offsets, so a field the caller treats
// as a pointer or an FVector while the engine calls it something narrower puts
// the tail of that write past the end of the buffer. Ask this before every such
// write, and stand the feature down rather than perform it.
//
// Subtraction rather than `Offset + bytes <= frame_size`: the offsets come from
// process memory that may not be a property table at all, and the sum of two
// values read out of noise can wrap past the check.
inline bool FieldFits(const FieldInfo& f, std::size_t bytes, std::size_t frame_size) {
    if (f.Size < bytes) return false;
    if (f.Offset > frame_size) return false;
    return frame_size - f.Offset >= bytes;
}

// Total size of a UStruct's instance data (UStruct::PropertiesSize). For a
// UFunction this covers the whole parameter frame including any locals, so it
// is the size to allocate for a ProcessEvent call.
std::size_t StructSize(std::uintptr_t ustruct);

// UObject::ClassPrivate, guarded. Zero when the object will not read, which is
// how a freed or not-yet-constructed one answers.
std::uintptr_t ClassOf(std::uintptr_t obj);

// Whether `cls` is `baseName` or derives from it, by walking the SuperStruct
// chain and comparing UClass names.
//
// UObject::ProcessEvent does not check that the UFunction it is given belongs to
// the object it is given: it builds a frame and invokes the native thunk, which
// casts the context to the class the function was declared on. So a caller that
// found its target by NAME has to prove the class too, or a same-named object of
// an unrelated class takes a typed write into the middle of whatever it really
// is - a success at the time, and heap corruption later.
bool ClassDerivesFrom(std::uintptr_t cls, const char* baseName);

// One named property, searching the SuperStruct chain as well. A live player
// controller is some game subclass, and the fields the mod reads off it
// (AController::Pawn) are declared several classes up, so a search of the
// leaf class alone finds nothing.
bool FindPropertyInChain(std::uintptr_t ustruct, const char* name, FieldInfo& out);

// Resolve several named properties at once and sanity-check the lot: every name
// present, every offset plus size inside StructSize(). Logs the whole table and
// returns false when any check fails, so a caller can stand its feature down
// with a diagnostic instead of writing into a buffer at guessed offsets.
bool ResolveAll(const char* label, std::uintptr_t ustruct,
                const std::vector<std::string>& names,
                std::vector<FieldInfo>& out);

// Log a struct's whole property table. Used by the discovery pass and when
// ResolveAll fails.
void DumpProperties(const char* label, std::uintptr_t ustruct);

// Prove the build profile's ReflectionLayout actually fits this game build.
//
// The numbers in it are engine-version-bound, and a set copied from a sibling
// mod on a different engine version does not fail loudly - it walks the FField
// chain through the wrong pointers and hands back offsets made of heap noise,
// which is how a Kismet parameter frame gets written at the wrong places. So
// the mod resolves one struct whose layout is already known independently -
// FMinimalViewInfo, whose fields the render caller copies at visible offsets -
// and checks the answers match. Everything reflection-backed waits on this.
//
// Does the work once and answers from cache. False also while the object table
// has not reached the struct yet, which is normal on the first frames, so the
// caller retries rather than treating it as a verdict.
bool ValidateLayout();

}  // namespace tow_ht::ue_reflect

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ue_reflect.h"

// The parts of a Kismet trace call that the aim cast and the lean sweep hold in
// common.
//
// Both dispatch a UKismetSystemLibrary trace through the script VM, both build
// the parameter frame in a stack buffer at offsets the engine's reflection data
// reported, and both therefore have the same two obligations before writing a
// byte: cap the frame, and prove every slot is wide enough for the fixed-size
// C++ type going into it. The two cast-specific things - which parameters exist
// and what the answer means - stay in aim_trace.cpp and lean_trace.cpp.
namespace tow_ht::kismet_frame {

// The parameter frame of a Kismet trace is a couple of hundred bytes. This is
// the ceiling the resolvers check PropertiesSize against, so a garbage size
// cannot turn into a stack overflow.
constexpr std::size_t kMaxParams = 1024;

// How many times a trace's KismetSystemLibrary lookup is retried before it is
// written off.
//
// FindLiveObject walks the whole GUObjectArray and builds a std::string per live
// object, so an unresolvable name is not something to retry forever: at the
// resolve retry's 250ms that is a full table walk four times a second, on the
// game thread, for the rest of the session. The objects appear at engine init,
// so anything still missing after this many attempts is missing.
constexpr int kMaxLookupAttempts = 40;   // 10s at the 250ms retry interval

// TArray's header, which is what a by-reference array parameter is in the
// frame. Both traces take ActorsToIgnore by const reference and neither call
// reallocates it, so the caller's backing store is enough and there is nothing
// for the engine to free.
struct alignas(8) TArrayHeader {
    void*        Data;
    std::int32_t Num;
    std::int32_t Max;
};

// One parameter and the fixed-size type the mod writes into its slot.
struct ExpectedWidth {
    const char* Name;
    std::size_t Index;   // into the vector ue_reflect::ResolveAll filled
    std::size_t Bytes;
};

// FirstMisfit's answer when every slot is wide enough.
constexpr std::size_t kAllFit = static_cast<std::size_t>(-1);

// The width table names a parameter slot the resolved frame does not have, so
// there is no slot to describe. Distinct from a position, because a caller
// handed a position looks up params[widths[i].Index] to describe it - which is
// the read this case exists to prevent.
constexpr std::size_t kIndexOutOfRange = static_cast<std::size_t>(-2);

// Index into `widths` of the first parameter whose slot cannot hold the type
// the mod writes there, or kAllFit.
//
// ResolveAll answers a different question: it proves a named field sits inside
// its own struct at the size THE ENGINE reports. That says nothing about the
// fixed-size type a caller is about to memcpy into the slot. A parameter frame
// is written into a stack buffer at these offsets, so a field the caller treats
// as a pointer or an FVector while the engine calls it something narrower puts
// the tail of that write past the end of the buffer.
//
// Returns rather than logs, so the answer is a pure function of its arguments
// and each caller can name its own cast in the line it writes.
//
// `params` holds one entry per name the caller asked ResolveAll for, and every
// `widths[i].Index` indexes into it. The correspondence is positional between
// two hand-maintained arrays in another file, expressed in no type, so it is
// checked rather than assumed: dropping or reordering a name in the parameter
// list without touching the width table compiles clean and would otherwise read
// past the end of the vector to build the offsets the frame is then written at,
// inside the one function whose whole job is preventing that write from
// overflowing. An out-of-range index answers kIndexOutOfRange rather than the
// offending position, for the reason given at that constant. Both answers mean
// "this frame is not the shape this mod writes", which is what callers act on.
inline std::size_t FirstMisfit(const std::vector<ue_reflect::FieldInfo>& params,
                               const ExpectedWidth* widths, std::size_t count,
                               std::size_t frameSize) {
    for (std::size_t i = 0; i < count; ++i) {
        if (widths[i].Index >= params.size()) return kIndexOutOfRange;
        if (!ue_reflect::FieldFits(params[widths[i].Index], widths[i].Bytes, frameSize))
            return i;
    }
    return kAllFit;
}

}  // namespace tow_ht::kismet_frame

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "aim_trace.h"

#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include <windows.h>

#include "kismet_frame.h"
#include "logging.h"
#include "ue_reflect.h"
#include "ue_vm.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace tow_ht::aim_trace {

namespace {

namespace ue = ::cameraunlock::unreal;

ue_vm::ResolveRetry g_resolveRetry;

struct Layout {
    std::size_t ParamsSize = 0;
    std::size_t WorldContext = 0;
    std::size_t Start = 0;
    std::size_t End = 0;
    std::size_t TraceChannel = 0;
    std::size_t TraceComplex = 0;
    std::size_t ActorsToIgnore = 0;
    std::size_t DrawDebugType = 0;
    std::size_t OutHit = 0;
    std::size_t IgnoreSelf = 0;
    std::size_t ReturnValue = 0;
    // Inside FHitResult.
    std::size_t ImpactPoint = 0;
};

Layout g_layout;
std::uintptr_t g_lineTraceFn = 0;
std::uintptr_t g_kismetSystemCdo = 0;
bool g_ready = false;
bool g_failed = false;
int  g_traceChannel = 0;
int  g_lookupAttempts = 0;

// Whether the last dispatch faulted. A cast that resolves and then faults every
// frame reverts the reticle to a direction projection, which is the drift this
// file exists to remove, so it cannot be silent.
bool g_dispatchFailing = false;
// Capped like every other repeating line in this mod. An object going valid and
// invalid across frames would otherwise write two lines per frame on the game
// thread, each an unbuffered locked write.
constexpr int kMaxDispatchLines = 8;
int g_dispatchLines = 0;

// One ignored actor, pointed at by the TArray the parameter frame carries.
// LineTraceSingle takes ActorsToIgnore by const reference and never
// reallocates it, so a static backing store is enough and there is nothing for
// the engine to free.
std::uintptr_t g_ignoreStorage = 0;

void GiveUp(const char* what) {
    if (g_failed) return;
    g_failed = true;
    Log::Line("aim-trace: %s. The crosshair falls back to the aim DIRECTION, which "
              "is exact at any range while the head is centred; once you lean there is "
              "no depth to work the parallax from, so nothing is marked.", what);
}

bool Resolve() {
    if (g_ready) return true;
    if (g_failed) return false;
    if (!g_resolveRetry.Due()) return false;
    if (!ue_vm::Ready()) return false;

    g_kismetSystemCdo = ue::FindLiveObject(
        "KismetSystemLibrary", "Default__KismetSystemLibrary", nullptr);
    g_lineTraceFn = ue::FindLiveObject("Function", "LineTraceSingle", "KismetSystemLibrary");
    if (!g_kismetSystemCdo || !g_lineTraceFn) {
        // The object table is walked from the render caller, so the first frames
        // can run before these are visible. Only repeated failure is a fault,
        // and the retry above paces it.
        if (++g_lookupAttempts >= kismet_frame::kMaxLookupAttempts)
            GiveUp("KismetSystemLibrary::LineTraceSingle is not in this build's object "
                   "table");
        return false;
    }

    static const std::vector<std::string> kParams = {
        "WorldContextObject", "Start", "End", "TraceChannel", "bTraceComplex",
        "ActorsToIgnore", "DrawDebugType", "OutHit", "bIgnoreSelf", "ReturnValue",
    };
    std::vector<ue_reflect::FieldInfo> p;
    if (!ue_reflect::ResolveAll("LineTraceSingle", g_lineTraceFn, kParams, p)) {
        GiveUp("LineTraceSingle's parameter frame did not resolve");
        return false;
    }

    const std::size_t paramsSize = ue_reflect::StructSize(g_lineTraceFn);
    if (paramsSize > kismet_frame::kMaxParams) {
        GiveUp("LineTraceSingle's parameter frame is not a Kismet trace frame");
        return false;
    }

    // Each of these puts a fixed-size C++ type into a slot whose offset came out
    // of the engine's reflection data, so each has to be proved wide enough
    // first - see kismet_frame.h.
    static const kismet_frame::ExpectedWidth kWidths[] = {
        {"WorldContextObject", 0, sizeof(std::uintptr_t)},
        {"Start",              1, sizeof(UeVector)},
        {"End",                2, sizeof(UeVector)},
        {"TraceChannel",       3, 1},
        {"bTraceComplex",      4, 1},
        {"ActorsToIgnore",     5, sizeof(kismet_frame::TArrayHeader)},
        {"DrawDebugType",      6, 1},
        {"bIgnoreSelf",        8, 1},
        {"ReturnValue",        9, 1},
    };
    const std::size_t misfit = kismet_frame::FirstMisfit(
        p, kWidths, std::size(kWidths), paramsSize);
    if (misfit == kismet_frame::kIndexOutOfRange) {
        Log::Line("aim-trace: LineTraceSingle's parameter frame is missing a slot "
                  "this mod writes");
        GiveUp("the cast's parameter frame is not the shape this mod writes");
        return false;
    }
    if (misfit != kismet_frame::kAllFit) {
        const kismet_frame::ExpectedWidth& w = kWidths[misfit];
        Log::Line("aim-trace: LineTraceSingle.%s is %zu bytes at +0x%zx in a %zu-byte "
                  "frame, too narrow for the %zu this mod writes",
                  w.Name, p[w.Index].Size, p[w.Index].Offset, paramsSize, w.Bytes);
        GiveUp("the cast's parameter frame is not the shape this mod writes");
        return false;
    }

    const std::uintptr_t hitResult = ue::FindLiveObject("ScriptStruct", "HitResult", nullptr);
    std::vector<ue_reflect::FieldInfo> h;
    if (!hitResult ||
        !ue_reflect::ResolveAll("HitResult", hitResult, {"ImpactPoint"}, h)) {
        GiveUp("FHitResult's layout did not resolve");
        return false;
    }
    // A pre-LWC FVector is three floats. Anything else says the field found is
    // not the one meant, and reading three floats out of it would produce a
    // plausible-looking aim point made of whatever follows.
    if (h[0].Size != sizeof(UeVector)) {
        Log::Line("aim-trace: FHitResult::ImpactPoint is %zu bytes, expected %zu",
                  h[0].Size, sizeof(UeVector));
        GiveUp("FHitResult::ImpactPoint is not a 3-float FVector");
        return false;
    }
    const std::size_t hitSize = ue_reflect::StructSize(hitResult);
    if (!ue_reflect::FieldFits(p[7], hitSize, paramsSize)) {
        GiveUp("LineTraceSingle.OutHit cannot hold a whole FHitResult");
        return false;
    }

    g_layout.ParamsSize     = paramsSize;
    g_layout.WorldContext   = p[0].Offset;
    g_layout.Start          = p[1].Offset;
    g_layout.End            = p[2].Offset;
    g_layout.TraceChannel   = p[3].Offset;
    g_layout.TraceComplex   = p[4].Offset;
    g_layout.ActorsToIgnore = p[5].Offset;
    g_layout.DrawDebugType  = p[6].Offset;
    g_layout.OutHit         = p[7].Offset;
    g_layout.IgnoreSelf     = p[8].Offset;
    g_layout.ReturnValue    = p[9].Offset;
    g_layout.ImpactPoint    = h[0].Offset;

    Log::Line("aim-trace: LineTraceSingle frame=%zu Start=+0x%zx End=+0x%zx "
              "OutHit=+0x%zx Return=+0x%zx | FHitResult ImpactPoint=+0x%zx | channel=%d",
        g_layout.ParamsSize, g_layout.Start, g_layout.End, g_layout.OutHit,
        g_layout.ReturnValue, g_layout.ImpactPoint, g_traceChannel);
    g_ready = true;
    return true;
}

}  // namespace

bool Ready()  { return Resolve(); }
bool Failed() { return g_failed; }
bool DispatchFailing() { return g_dispatchFailing; }
void SetTraceChannel(int channel) { g_traceChannel = channel; }

Result Cast(std::uintptr_t pawn, const UeVector& start, const UeVector& dir,
            float maxDistance) {
    Result r;
    if (!Resolve() || pawn == 0) return r;

    alignas(16) unsigned char buf[kismet_frame::kMaxParams];
    std::memset(buf, 0, g_layout.ParamsSize);

    // bIgnoreSelf makes LineTraceSingle exclude the actor it is handed as the
    // world context, which is why the PAWN goes here rather than the
    // controller: the ray starts at the eye, inside the player's own capsule,
    // and without this it reports an initial overlap at zero distance every
    // single frame.
    std::memcpy(buf + g_layout.WorldContext, &pawn, sizeof(pawn));
    std::memcpy(buf + g_layout.Start, &start, sizeof(start));
    const UeVector to{
        start.X + dir.X * maxDistance,
        start.Y + dir.Y * maxDistance,
        start.Z + dir.Z * maxDistance,
    };
    std::memcpy(buf + g_layout.End, &to, sizeof(to));
    buf[g_layout.TraceChannel] = static_cast<unsigned char>(g_traceChannel);
    // Simple collision. bTraceComplex is an either/or rather than an also: a
    // complex query accepts only trimesh shapes, and a character capsule has
    // none, so a complex ray passes through the NPC being aimed at and stops on
    // whatever is behind. The crosshair would then carry the parallax for that
    // depth while the round converges on the NPC, which is the
    // lean * (1/d0 - 1/d) error this cast exists to remove. Which shapes THIS
    // game's own weapon trace stops on has not been measured; simple is the
    // choice that at least cannot miss a character outright.
    buf[g_layout.TraceComplex] = 0;
    buf[g_layout.DrawDebugType] = 0;  // EDrawDebugTrace::None
    buf[g_layout.IgnoreSelf] = 1;

    g_ignoreStorage = pawn;
    kismet_frame::TArrayHeader ignore{ &g_ignoreStorage, 1, 1 };
    std::memcpy(buf + g_layout.ActorsToIgnore, &ignore, sizeof(ignore));

    if (!ue_vm::Dispatch(reinterpret_cast<void*>(g_kismetSystemCdo),
                         reinterpret_cast<void*>(g_lineTraceFn), buf)) {
        if (!g_dispatchFailing) {
            g_dispatchFailing = true;
            if (g_dispatchLines++ < kMaxDispatchLines) {
                Log::Line("aim-trace: the cast is faulting - the crosshair follows the "
                          "aim DIRECTION while the head is centred, and is not marked "
                          "at all once you lean, because the depth the lean needs is "
                          "unknown");
            }
        }
        return r;
    }
    if (g_dispatchFailing) {
        g_dispatchFailing = false;
        if (g_dispatchLines++ < kMaxDispatchLines)
            Log::Line("aim-trace: the cast is answering again, live impact depth "
                      "restored");
    }

    r.Valid = true;
    r.Hit = buf[g_layout.ReturnValue] != 0;
    if (r.Hit) {
        std::memcpy(&r.Point, buf + g_layout.OutHit + g_layout.ImpactPoint,
                    sizeof(UeVector));
        // Measured from the contact's own world position projected onto the aim
        // direction, not from the FHitResult Distance field: a position
        // projected onto a direction is a distance by construction, while a
        // field named Distance is whatever the engine chose to put there.
        r.Distance = (r.Point.X - start.X) * dir.X
                   + (r.Point.Y - start.Y) * dir.Y
                   + (r.Point.Z - start.Z) * dir.Z;
    }
    return r;
}

}  // namespace tow_ht::aim_trace

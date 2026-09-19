// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "lean_trace.h"

#include <cmath>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include <windows.h>

#include "kismet_frame.h"
#include "logging.h"
#include "ue4_types.h"
#include "ue_reflect.h"
#include "ue_vm.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace tow_ht::lean_trace {

namespace {

namespace ue = ::cameraunlock::unreal;
using cameraunlock::camera::LeanObstruction;
using cameraunlock::math::Vec3;

ue_vm::ResolveRetry g_resolveRetry;

struct Layout {
    std::size_t ParamsSize = 0;
    std::size_t WorldContext = 0;
    std::size_t Start = 0;
    std::size_t End = 0;
    std::size_t Radius = 0;
    std::size_t TraceChannel = 0;
    std::size_t TraceComplex = 0;
    std::size_t ActorsToIgnore = 0;
    std::size_t DrawDebugType = 0;
    std::size_t OutHit = 0;
    std::size_t IgnoreSelf = 0;
    // Inside FHitResult.
    std::size_t BlockingHit = 0;
    std::size_t HitLocation = 0;
    std::size_t HitNormal = 0;
};

Layout g_layout;
std::uintptr_t g_sphereTraceFn = 0;
std::uintptr_t g_kismetSystemCdo = 0;
std::uintptr_t g_pawn = 0;
bool  g_ready = false;
bool  g_failed = false;
float g_radius = 12.0f;
int   g_channel = 0;
int   g_lookupAttempts = 0;

// A swept sphere whose start already overlaps something answers blocked with the
// centre where it started, so the clamp allows nothing and the lean dies in
// EVERY direction, including away from the obstruction. In first person the eye
// routinely sits within the sweep radius of a doorframe, a railing or a low
// overhang, so this is not an edge case: it reads as the positional half of head
// tracking cutting out and swimming back in around every doorway, and it logs
// identically to a wall correctly stopping a lean.
//
// bStartPenetrating cannot be read to tell them apart. FHitResult declares it
// and bBlockingHit as adjacent one-bit fields, FBoolProperty reports the offset
// of the byte they share rather than of the bit, and this profile's
// ReflectionLayout does not carry the ByteMask needed to pick one out - `& 1u`
// would return bBlockingHit for both.
//
// The hit normal separates the two. An obstruction only bounds a lean that
// travels INTO it, so a zero-travel contact whose surface normal opposes the
// lean is a wall the eye is already against and the clamp should stop there.
//
// A zero-travel contact that does NOT oppose the lean is the other case, and it
// is not a clear path: a sweep that terminates on its initial overlap has
// reported nothing about the geometry further along the ray. Answering "clear"
// would send the full lean through a wall the sweep never got to test - stand
// beside a doorframe with another wall 8cm ahead and lean forward. So it is
// reported as a query that could not run, which passes the lean through
// unclamped exactly as before AND says so through LastQueryFailed(), rather
// than as a room that was looked at and found open.
// The epsilon is what makes the answer stable rather than a coin toss. Both
// vectors are unit length, so the dot product is the cosine of the angle between
// them, and an eye resting against a wall while the head leans ALONG it puts
// that cosine at zero - where the sign is set by tracker noise. The two answers
// are a full lean and no lean at all, so a flapping sign is a strobing camera.
// Biasing the threshold a few degrees past square sends the ambiguous band to
// "opposing", which is the conservative answer: the lean stops.
constexpr float kOpposingCos = 0.05f;

bool OpposesLean(const UeVector& normal, const Vec3& direction) {
    return normal.X * direction.x + normal.Y * direction.y +
           normal.Z * direction.z < kOpposingCos;
}

void GiveUp(const char* what) {
    if (g_failed) return;
    g_failed = true;
    Log::Line("lean-trace: %s. The lean runs UNCLAMPED for this session - head "
              "position still works, but leaning into a wall will put the view "
              "through it.", what);
}

bool Resolve() {
    if (g_ready) return true;
    if (g_failed) return false;
    if (!g_resolveRetry.Due()) return false;
    if (!ue_vm::Ready()) return false;

    g_kismetSystemCdo = ue::FindLiveObject(
        "KismetSystemLibrary", "Default__KismetSystemLibrary", nullptr);
    g_sphereTraceFn =
        ue::FindLiveObject("Function", "SphereTraceSingle", "KismetSystemLibrary");
    if (!g_kismetSystemCdo || !g_sphereTraceFn) {
        if (++g_lookupAttempts >= kismet_frame::kMaxLookupAttempts)
            GiveUp("KismetSystemLibrary::SphereTraceSingle is not in this build's object "
                   "table");
        return false;
    }

    static const std::vector<std::string> kParams = {
        "WorldContextObject", "Start", "End", "Radius", "TraceChannel",
        "bTraceComplex", "ActorsToIgnore", "DrawDebugType", "OutHit", "bIgnoreSelf",
    };
    std::vector<ue_reflect::FieldInfo> p;
    if (!ue_reflect::ResolveAll("SphereTraceSingle", g_sphereTraceFn, kParams, p)) {
        GiveUp("SphereTraceSingle's parameter frame did not resolve");
        return false;
    }

    const std::size_t paramsSize = ue_reflect::StructSize(g_sphereTraceFn);
    if (paramsSize > kismet_frame::kMaxParams) {
        GiveUp("SphereTraceSingle's parameter frame is not a Kismet trace frame");
        return false;
    }

    // Each of these puts a fixed-size C++ type into a slot whose offset came out
    // of the engine's reflection data, so each has to be proved wide enough
    // first - see kismet_frame.h.
    static const kismet_frame::ExpectedWidth kWidths[] = {
        {"WorldContextObject", 0, sizeof(std::uintptr_t)},
        {"Start",              1, sizeof(UeVector)},
        {"End",                2, sizeof(UeVector)},
        {"Radius",             3, sizeof(float)},
        {"TraceChannel",       4, 1},
        {"bTraceComplex",      5, 1},
        {"ActorsToIgnore",     6, sizeof(kismet_frame::TArrayHeader)},
        {"DrawDebugType",      7, 1},
        {"bIgnoreSelf",        9, 1},
    };
    const std::size_t misfit = kismet_frame::FirstMisfit(
        p, kWidths, std::size(kWidths), paramsSize);
    if (misfit == kismet_frame::kIndexOutOfRange) {
        Log::Line("lean-trace: SphereTraceSingle's parameter frame is missing a slot "
                  "this mod writes");
        GiveUp("the sweep's parameter frame is not the shape this mod writes");
        return false;
    }
    if (misfit != kismet_frame::kAllFit) {
        const kismet_frame::ExpectedWidth& w = kWidths[misfit];
        Log::Line("lean-trace: SphereTraceSingle.%s is %zu bytes at +0x%zx in a "
                  "%zu-byte frame, too narrow for the %zu this mod writes",
                  w.Name, p[w.Index].Size, p[w.Index].Offset, paramsSize, w.Bytes);
        GiveUp("the sweep's parameter frame is not the shape this mod writes");
        return false;
    }

    const std::uintptr_t hitResult = ue::FindLiveObject("ScriptStruct", "HitResult", nullptr);
    std::vector<ue_reflect::FieldInfo> h;
    if (!hitResult ||
        !ue_reflect::ResolveAll("HitResult", hitResult,
                                {"bBlockingHit", "Location", "Normal"}, h)) {
        GiveUp("FHitResult's layout did not resolve");
        return false;
    }
    // Location, NOT ImpactPoint. For a swept sphere ImpactPoint is the point on
    // the surface, while Location is where the sphere's CENTRE stopped - which
    // is the surface point backed off by the radius, and the radius is the
    // standoff. Taking ImpactPoint here would put the eye on the wall and make
    // the clamp's skin do the standoff twice.
    if (h[1].Size != sizeof(UeVector) || h[2].Size != sizeof(UeVector)) {
        Log::Line("lean-trace: FHitResult::Location is %zu bytes and ::Normal is %zu, "
                  "expected %zu each", h[1].Size, h[2].Size, sizeof(UeVector));
        GiveUp("FHitResult::Location or ::Normal is not a 3-float FVector");
        return false;
    }

    const std::size_t hitSize = ue_reflect::StructSize(hitResult);
    if (!ue_reflect::FieldFits(p[8], hitSize, paramsSize)) {
        GiveUp("SphereTraceSingle.OutHit cannot hold a whole FHitResult");
        return false;
    }

    g_layout.ParamsSize     = paramsSize;
    g_layout.WorldContext   = p[0].Offset;
    g_layout.Start          = p[1].Offset;
    g_layout.End            = p[2].Offset;
    g_layout.Radius         = p[3].Offset;
    g_layout.TraceChannel   = p[4].Offset;
    g_layout.TraceComplex   = p[5].Offset;
    g_layout.ActorsToIgnore = p[6].Offset;
    g_layout.DrawDebugType  = p[7].Offset;
    g_layout.OutHit         = p[8].Offset;
    g_layout.IgnoreSelf     = p[9].Offset;
    g_layout.BlockingHit    = h[0].Offset;
    g_layout.HitLocation    = h[1].Offset;
    g_layout.HitNormal      = h[2].Offset;

    Log::Line("lean-trace: SphereTraceSingle frame=%zu Start=+0x%zx End=+0x%zx "
              "Radius=+0x%zx OutHit=+0x%zx | FHitResult bBlockingHit=+0x%zx "
              "Location=+0x%zx Normal=+0x%zx | radius=%.1fcm channel=%d",
        g_layout.ParamsSize, g_layout.Start, g_layout.End, g_layout.Radius,
        g_layout.OutHit, g_layout.BlockingHit, g_layout.HitLocation,
        g_layout.HitNormal, g_radius, g_channel);
    g_ready = true;
    return true;
}

}  // namespace

void SetRadius(float centimetres) { g_radius = centimetres; }
void SetChannel(int traceTypeQuery) { g_channel = traceTypeQuery; }
void SetPawn(std::uintptr_t pawn) { g_pawn = pawn; }

bool Ready()  { return Resolve(); }
bool Failed() { return g_failed; }

LeanObstruction Query(void*, const Vec3& start, const Vec3& direction, float maxDistance) {
    LeanObstruction out;
    if (!Resolve() || g_pawn == 0) return out;

    alignas(16) unsigned char buf[kismet_frame::kMaxParams];
    std::memset(buf, 0, g_layout.ParamsSize);

    // bIgnoreSelf makes the sweep exclude the actor it is handed as the world
    // context, which is why the PAWN goes here rather than the controller: the
    // sphere starts at the eye, inside the player's own capsule, and without
    // this it reports an initial overlap at zero distance every frame and the
    // lean stops working everywhere.
    std::memcpy(buf + g_layout.WorldContext, &g_pawn, sizeof(g_pawn));

    const UeVector from{start.x, start.y, start.z};
    const UeVector to{start.x + direction.x * maxDistance,
                      start.y + direction.y * maxDistance,
                      start.z + direction.z * maxDistance};
    std::memcpy(buf + g_layout.Start, &from, sizeof(from));
    std::memcpy(buf + g_layout.End, &to, sizeof(to));
    const float radius = g_radius;
    std::memcpy(buf + g_layout.Radius, &radius, sizeof(radius));
    buf[g_layout.TraceChannel] = static_cast<unsigned char>(g_channel);
    buf[g_layout.TraceComplex] = 0;
    buf[g_layout.DrawDebugType] = 0;
    buf[g_layout.IgnoreSelf] = 1;

    // An empty TArray satisfies ActorsToIgnore: the sweep only reads it, and
    // bIgnoreSelf above already excludes the one actor that matters.
    const kismet_frame::TArrayHeader empty{nullptr, 0, 0};
    std::memcpy(buf + g_layout.ActorsToIgnore, &empty, sizeof(empty));

    if (!ue_vm::Dispatch(reinterpret_cast<void*>(g_kismetSystemCdo),
                         reinterpret_cast<void*>(g_sphereTraceFn), buf))
        return out;   // queried stays false: the clamp then passes the lean through

    // bBlockingHit rather than the return value: it is what the clamp's own
    // semantics are defined against, and it sits in the struct the distance and
    // normal are read from, so all three describe the same hit.
    out.queried = true;
    out.blocked = (buf[g_layout.OutHit + g_layout.BlockingHit] & 1u) != 0;
    if (out.blocked) {
        UeVector location{};
        std::memcpy(&location, buf + g_layout.OutHit + g_layout.HitLocation,
                    sizeof(location));
        const float dx = location.X - start.x;
        const float dy = location.Y - start.y;
        const float dz = location.Z - start.z;
        out.distance = std::sqrt(dx * dx + dy * dy + dz * dz);

        // Zero centre-travel is either a wall the eye is already against or a
        // sphere that started overlapping something off to the side. Only the
        // first bounds this lean; the second is a sweep that saw nothing beyond
        // the overlap - see OpposesLean.
        if (out.distance <= 0.0f) {
            UeVector normal{};
            std::memcpy(&normal, buf + g_layout.OutHit + g_layout.HitNormal,
                        sizeof(normal));
            if (!OpposesLean(normal, direction)) {
                out.queried = false;
                out.blocked = false;
                out.distance = 0.0f;
            }
        }
    }
    return out;
}

}  // namespace tow_ht::lean_trace

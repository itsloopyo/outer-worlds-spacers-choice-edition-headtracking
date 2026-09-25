// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Unstick the crosshair.
//
// The weapon trace keeps the clean mouse-driven rotation while the view follows
// the head, so the crosshair drawn at the centre of the picture stops marking
// where the round goes the moment the player looks off-centre or leans. UMG
// anchors it there, so the fix is to translate the widget by the offset the aim
// projection computes.
//
// The mechanism is UWidget::SetRenderTranslation, dispatched by name through
// the script VM. A raw write to RenderTransform.Translation moves the property
// and not the pixels: the setter is what invalidates the Slate widget
// underneath.
//
// A HUD widget's name lives in a cooked Blueprint asset, not in the exe, so it
// can only be read off a running game - widget_probe.cpp is what reads it, and
// kCrosshairWidgets (crosshair_widgets.h) is where the answer goes.

#include "reticle_mover.h"

#include "crosshair_widgets.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>

#include "aim_projection.h"
#include "builds/build_registry.h"
#include "logging.h"
#include "ue4_types.h"
#include "ue_reflect.h"
#include "ue_vm.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace tow_ht::ReticleMover {

namespace {

namespace ue = ::cameraunlock::unreal;

// How often the object table is re-walked to re-find the targets: the short
// interval once a held widget has failed its liveness test, the long one as a
// backstop while everything still looks fine. The backstop bounds how long the
// crosshair can sit dead at screen centre if a rebuilt HUD leaves the widget we
// hold alive but no longer painted, which no test on the pointer itself can
// see.
constexpr std::uint64_t kRetryWalkMs    = 2000;
constexpr std::uint64_t kBackstopWalkMs = 15000;

// How often a push happens anyway with the offset unchanged, so a game-side
// reset of RenderTransform cannot stick. Counted in calls, which arrive at
// frame rate.
constexpr std::uint64_t kReassertEveryCalls = 120;

// The offset line is the evidence that the crosshair follows the shot, and that
// reads off the first few lines; the heartbeat carries liveness for the rest of
// the session.
constexpr std::uint64_t kOffsetLogMs    = 2000;
constexpr int           kOffsetLogLines = 20;

// What a target has to BE, not just be called. The push is a
// UWidget::SetRenderTranslation dispatched through the script VM, and the thunk
// behind it casts its context to UWidget without checking - so a texture, a
// sound or an actor that happens to be named Crosshair would take a write into
// the middle of whatever it really is. The names come out of a user-editable
// ini, so a collision needs no exotic input.
constexpr const char* kWidgetClassName = "Widget";

// How far up the outer chain the target test looks. A crosshair image sits
// several widget trees deep inside its HUD, and the live one and the Blueprint
// template it was built from share every link until the very top - the live
// tree ends at the game instance, the template's at its own asset package - so
// the walk has to reach that far or the outer test cannot tell them apart.
constexpr int kOuterChainDepth = 6;

// What a viewport scale can believably be. UE's default curve gives 1.0 at
// 1080p and 1.333 at 1440p-tall; outside this the call did not return what we
// think it did.
constexpr float kMinPlausibleDpiScale = 0.05f;
constexpr float kMaxPlausibleDpiScale = 20.0f;

struct Target {
    std::string Name;
    std::string Outer;   // empty = no outer requirement
};
std::vector<Target> g_targets;

// A widget plus the class pointer it carried when collected. Loading a level
// frees and recreates the HUD, so a held pointer can dangle or be reused for a
// different object.
struct Widget { std::uintptr_t Obj = 0; std::uintptr_t Cls = 0; };
std::vector<Widget> g_widgets;

// FName comparison ids for the target names, learned by the first walk that
// finds them. ObjectName() ignores the FName number, so comparing the id is the
// same test as the string compare that learned it, minus a name-pool lookup and
// a std::string build for every one of ~100k objects - which is what makes
// re-walking on a timer affordable.
std::vector<std::uint32_t> g_nameIds;

float g_lastWalkMs = 0.0f;
// Whether s_lastX/s_lastY in NeedsPush describe the widgets currently held.
// Cleared when the walk swaps the set, so the replacements get their first push
// immediately rather than at the next reassert.
bool g_pushDedupValid = false;
// [Dev] PoseLog: keep writing the offset line for a whole measuring session
// rather than stopping after the first few.
bool g_uncappedLog = false;
std::uintptr_t g_setRenderTranslationFn = 0;
std::uintptr_t g_getViewportScaleFn = 0;
std::uintptr_t g_widgetLayoutLibCdo = 0;

// UMG paints a widget's render translation in slate units, which become
// units * DPI-scale real pixels, while the projected offset is real pixels. The
// push divides by this.
float g_dpiScale = 1.0f;

bool Live(const Widget& w) {
    if (!w.Obj || !w.Cls) return false;
    std::uintptr_t cls = 0;
    if (!ue::SafeReadPtr(w.Obj + Offsets().UObjectGlobals.kClassPrivate, cls) || cls != w.Cls)
        return false;
    return ue_vm::IsRegistered(w.Obj);
}

// The two parameter frames this file builds by hand. Both are one parameter
// wide, so they are written as structs rather than through the reflected-offset
// path kismet_frame.h defines for the traces - but ProcessEvent still writes
// the WHOLE of the UFunction's frame into whatever buffer it is handed, so the
// tail padding is what covers a trailing parameter the signature carries and
// this file does not set. UE 4.27: FVector2D is two floats.
struct alignas(8) TranslationFrame { UeVector2D Translation; char pad[16]; };
struct alignas(8) ViewportScaleFrame { void* WorldContext; float Ret; char pad[12]; };

bool g_translationRejected = false;
bool g_viewportScaleRejected = false;

// Prove the engine's own frame for `fn` fits the buffer it will be handed.
// Never call a UFunction whose frame is wider: ProcessEvent would write past
// the end of a stack struct.
bool FrameFits(std::uintptr_t fn, std::size_t bufferBytes, const char* name) {
    const std::size_t frame = ue_reflect::StructSize(fn);
    if (frame != 0 && frame <= bufferBytes) return true;
    Log::Line("reticle: %s has a %zu-byte parameter frame, which does not fit the "
              "%zu bytes this mod hands it - the call stands down, so the "
              "crosshair stays where the game puts it",
        name, frame, bufferBytes);
    return false;
}

// The UFUNCTIONs the mover dispatches, plus the class-default object the
// viewport-scale call needs a `this` for. Each is looked up once and kept: they
// live for the process, unlike the widgets, which a level change replaces.
//
// Deferred entirely while the profile's ReflectionLayout is unproven, because a
// frame width read through a layout that does not fit this build is not a
// width. view_hook already withholds the reticle on exactly those frames, so
// nothing that worked before is now waiting on something new.
void ResolveScriptFunctions() {
    if (!ue_reflect::ValidateLayout()) return;

    if (!g_setRenderTranslationFn && !g_translationRejected) {
        const std::uintptr_t fn =
            ue::FindLiveObject("Function", "SetRenderTranslation", "Widget");
        if (fn) {
            if (FrameFits(fn, sizeof(TranslationFrame), "UWidget::SetRenderTranslation"))
                g_setRenderTranslationFn = fn;
            else
                g_translationRejected = true;
        }
    }
    if (!g_getViewportScaleFn && !g_viewportScaleRejected) {
        const std::uintptr_t fn =
            ue::FindLiveObject("Function", "GetViewportScale", "WidgetLayoutLibrary");
        if (fn) {
            if (FrameFits(fn, sizeof(ViewportScaleFrame),
                          "UWidgetLayoutLibrary::GetViewportScale"))
                g_getViewportScaleFn = fn;
            else
                g_viewportScaleRejected = true;
        }
    }
    if (!g_widgetLayoutLibCdo)
        g_widgetLayoutLibCdo = ue::FindLiveObject("WidgetLayoutLibrary", "Default__WidgetLayoutLibrary", nullptr);
}

// What one walk found, next to what was already held.
//
// The first walk always reports, then only changes. Without the first, a build
// whose widget names do not match produces found == held == 0 on every walk,
// `changed` never goes true, and the one line written to explain that exact
// state - "(NOT FOUND - stays screen-fixed)" - is unreachable in it.
void ReportCollected(const std::vector<Widget>& found, const std::vector<int>& matches,
                     const std::vector<int>& rejected, bool changed) {
    static bool s_reported = false;
    if (!changed && s_reported) return;
    s_reported = true;
    for (std::size_t i = 0; i < g_targets.size(); ++i) {
        Log::Line("reticle: target %-24s 0x%llx -> 0x%llx  matches=%d "
                  "nonWidgetNameMatches=%d%s",
            g_targets[i].Name.c_str(),
            static_cast<unsigned long long>(g_widgets[i].Obj),
            static_cast<unsigned long long>(found[i].Obj), matches[i], rejected[i],
            found[i].Obj ? "" : "  (NOT FOUND - stays screen-fixed)");
    }
    Log::Line("reticle: setRenderTranslation=0x%llx dpiScale=%.3f walk=%.1fms",
        static_cast<unsigned long long>(g_setRenderTranslationFn), g_dpiScale, g_lastWalkMs);
}

// One table walk collects every target; doing it per widget would walk the
// whole object array once each.
void Collect() {
    // The class test walks the SuperStruct chain, so it needs the same proof
    // everything reflection-backed waits on. Run against an unproved layout it
    // reads a wrong offset, every name match lands in `rejected`, and the log
    // then blames the widget names - sending the next session to re-run the
    // probe when the fault is the profile.
    if (!ue_reflect::ValidateLayout()) return;

    LARGE_INTEGER freq{}, t0{}, t1{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    const std::size_t n = g_targets.size();
    std::vector<Widget> found(n);
    std::vector<int> matches(n, 0);
    // Names that matched but were not widgets. Counted separately because
    // "the name is wrong" and "the class test rejected it" need opposite fixes -
    // re-run the widget probe versus re-derive the reflection layout - and
    // without this they produce the same "NOT FOUND" line.
    std::vector<int> rejected(n, 0);
    const std::size_t nameOff = Offsets().UObjectGlobals.kNamePrivate;
    ue::ForEachUObject([&](std::uintptr_t obj) -> bool {
        std::uint32_t id = 0;
        if (!ue::SafeReadU32(obj + nameOff, id)) return false;
        std::string name;
        for (std::size_t i = 0; i < n; ++i) {
            if (g_nameIds[i] != 0) {
                if (id != g_nameIds[i]) continue;
            } else {
                if (name.empty()) name = ue::ResolveFName(id);
                if (!ue::EqualsCI(name, g_targets[i].Name.c_str())) continue;
            }
            // A class-default object's widget is a template that is never
            // painted; moving it changes nothing on screen while looking exactly
            // like success - the log reports a match and a live pointer, the
            // heartbeat reports the mover holding, and the crosshair does not
            // move.
            //
            // The test is on the OUTER chain, not the candidate's own name. The
            // template inside Default__HUD_BP_C's widget tree is itself named
            // `Crosshair`, exactly like the live one, so a name test only ever
            // caught a CDO named as a target - which no target is.
            if (name.empty()) name = ue::ResolveFName(id);
            const std::string outerChain = ue_vm::OuterChain(obj, kOuterChainDepth, "/");
            if (ue::ContainsCI(outerChain, "Default__")) continue;
            if (!g_targets[i].Outer.empty() &&
                !ue::ContainsCI(outerChain, g_targets[i].Outer.c_str())) continue;
            std::uintptr_t cls = 0;
            if (!ue::SafeReadPtr(obj + Offsets().UObjectGlobals.kClassPrivate, cls) ||
                !cls) {
                break;
            }
            if (!ue_reflect::ClassDerivesFrom(cls, kWidgetClassName)) {
                ++rejected[i];
                break;
            }
            found[i] = {obj, cls};
            g_nameIds[i] = id;
            ++matches[i];
            break;
        }
        return false;
    });

    QueryPerformanceCounter(&t1);
    g_lastWalkMs = freq.QuadPart
        ? static_cast<float>((t1.QuadPart - t0.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart))
        : 0.0f;

    bool changed = false;
    for (std::size_t i = 0; i < n; ++i)
        if (found[i].Obj != g_widgets[i].Obj) changed = true;

    ResolveScriptFunctions();
    ReportCollected(found, matches, rejected, changed);

    // A rebuilt HUD gives back different widgets at the same head pose, and the
    // push de-duplicates on the OFFSET, so without this the new crosshair sits
    // at screen centre until either the head moves or the reassert comes round.
    if (changed) g_pushDedupValid = false;

    g_widgets = found;
}

void RefreshDpiScale() {
    if (!g_getViewportScaleFn || !g_widgetLayoutLibCdo) return;
    // GetViewportScale finds the viewport through its world-context object, so
    // there is nothing to ask while the walk has not found a widget to pass.
    std::uintptr_t context = 0;
    for (const Widget& w : g_widgets) if (w.Obj) { context = w.Obj; break; }
    if (!context) return;
    ViewportScaleFrame p{};
    p.WorldContext = reinterpret_cast<void*>(context);
    if (!ue_vm::Dispatch(reinterpret_cast<void*>(g_widgetLayoutLibCdo),
                         reinterpret_cast<void*>(g_getViewportScaleFn), &p))
        return;
    if (p.Ret > kMinPlausibleDpiScale && p.Ret < kMaxPlausibleDpiScale
        && p.Ret != g_dpiScale) {
        Log::Line("reticle: viewport DPI scale %.3f -> %.3f", g_dpiScale, p.Ret);
        g_dpiScale = p.Ret;
    }
}

void Push(Widget& w, float x, float y) {
    if (!Live(w)) return;
    TranslationFrame tr{};
    tr.Translation.X = x;
    tr.Translation.Y = y;
    if (ue_vm::Dispatch(reinterpret_cast<void*>(w.Obj),
                        reinterpret_cast<void*>(g_setRenderTranslationFn), &tr))
        return;

    // A dispatch that faults means the object stopped being what it was between
    // the liveness test and the call. Dropping it sends the next walk looking
    // for whatever replaced it; swallowing the fault would leave the crosshair
    // pinned to a dead object with nothing in the log to say so.
    Log::Line("reticle: SetRenderTranslation faulted on 0x%llx - dropped, re-locating",
        static_cast<unsigned long long>(w.Obj));
    w = Widget{};
}

bool VmReady() {
    if (!ue_vm::Ready()) return false;
    static bool s_announced = false;
    if (!s_announced) {
        s_announced = true;
        Log::Line("reticle: ProcessEvent at RVA 0x%08llx",
            static_cast<unsigned long long>(ue_vm::ProcessEventRva()));
    }
    return true;
}

void MaybeRefreshTargets() {
    static std::uint64_t s_lastWalk = 0;
    const std::uint64_t now = GetTickCount64();
    // Below the shorter of the two intervals neither answer sends the walk
    // round again, so the liveness test - half a dozen guarded reads per widget
    // - is skipped rather than run at frame rate to pick between them.
    if (s_lastWalk != 0 && now - s_lastWalk < kRetryWalkMs) return;

    bool allLive = !g_widgets.empty();
    for (const Widget& w : g_widgets) if (!Live(w)) { allLive = false; break; }
    if (s_lastWalk != 0 && allLive && now - s_lastWalk < kBackstopWalkMs) return;

    s_lastWalk = now;
    Collect();
    RefreshDpiScale();
}

// The hook fires several times per frame but the offset only changes on the
// render caller, and every push is a script-VM dispatch. Skip the pass when
// nothing moved, but re-assert every kReassertEveryCalls so a game-side reset
// of RenderTransform cannot stick while the offset is static.
bool NeedsPush(float x, float y) {
    static float s_lastX = 0.0f, s_lastY = 0.0f;
    static std::uint64_t s_calls = 0;
    const bool reassert = (s_calls++ % kReassertEveryCalls) == 0;
    if (g_pushDedupValid && !reassert && x == s_lastX && y == s_lastY) return false;
    s_lastX = x; s_lastY = y; g_pushDedupValid = true;
    return true;
}

// The reads Live() needs. Without them every widget tests dead, so every push is
// skipped, `Holding()` is false forever and the heartbeat says `searching` - and
// the 15s backstop never applies either, so the full object-table walk runs every
// 2s on the game thread to achieve none of it. Say so once and stop.
bool ObjectArrayReadable() {
    const auto& g = Offsets().UObjectGlobals;
    if (g.kObjObjects != 0 && g.kChunkNumElems != 0 && g.kFUObjectItemSize != 0)
        return true;
    static bool s_said = false;
    if (!s_said) {
        s_said = true;
        Log::Line("reticle: this build profile carries no GUObjectArray geometry "
                  "(ObjObjects=0x%zx ChunkNumElems=%zu ItemSize=%zu), so a widget "
                  "cannot be proved live - the crosshair stays where the game puts it",
                  static_cast<std::size_t>(g.kObjObjects), g.kChunkNumElems,
                  g.kFUObjectItemSize);
    }
    return false;
}

// A widget list: comma-separated `Name` or `Name@Outer`, with surrounding
// blanks trimmed and empty items dropped. A pure parse of the string, kept
// apart from the state it goes on to replace.
std::vector<Target> ParseTargets(const char* spec) {
    std::vector<Target> targets;
    if (!spec) return targets;
    const std::string s(spec);
    std::size_t start = 0;
    while (start <= s.size()) {
        std::size_t comma = s.find(',', start);
        if (comma == std::string::npos) comma = s.size();
        std::string item = s.substr(start, comma - start);
        start = comma + 1;
        while (!item.empty() && (item.front() == ' ' || item.front() == '\t')) item.erase(item.begin());
        while (!item.empty() && (item.back() == ' ' || item.back() == '\t')) item.pop_back();
        if (item.empty()) continue;
        Target t;
        const std::size_t at = item.find('@');
        if (at == std::string::npos) {
            t.Name = item;
        } else {
            t.Name = item.substr(0, at);
            t.Outer = item.substr(at + 1);
        }
        if (t.Name.empty()) continue;
        targets.push_back(t);
    }
    return targets;
}

// The offset the push was made with, capped then time-gated. The evidence that
// the crosshair follows the shot reads off the first few lines; the heartbeat
// carries liveness for the rest of the session.
void LogOffset(bool haveOffset, float dx, float dy, float slateX, float slateY) {
    static std::uint64_t s_lastOffsetLog = 0;
    static int s_offsetLines = 0;
    if (!g_uncappedLog && s_offsetLines >= kOffsetLogLines) return;
    const std::uint64_t now = GetTickCount64();
    if (s_offsetLines != 0 && now - s_lastOffsetLog < kOffsetLogMs) return;
    s_lastOffsetLog = now;
    ++s_offsetLines;
    // The impact pair describes the last frame that PROJECTED, so it is printed
    // only beside a valid offset. `cast` is set on every frame the hook pushes
    // one, so it is current either way and says why there is no offset.
    if (haveOffset) {
        Log::Line("reticle: offset px=(%.1f,%.1f) slate=(%.1f,%.1f) dpi=%.3f valid=yes "
                  "cast=%s impactPoint=%s dist=%.0fcm walk=%.1fms",
            dx, dy, slateX, slateY, g_dpiScale,
            AimProjection::LastCastRan() ? "ran" : "unavailable",
            AimProjection::LastHadImpactPoint() ? "live" : "none (direction)",
            AimProjection::LastImpactDistance(), g_lastWalkMs);
    } else {
        Log::Line("reticle: crosshair parked at centre, valid=no cast=%s walk=%.1fms",
            AimProjection::LastCastRan() ? "ran" : "unavailable", g_lastWalkMs);
    }
}

// What both entry points need before there is anything to push through. The
// two liveness reporters latch their own one-shot lines, so the order matters
// and the short circuit keeps it.
bool CanPush() {
    return !g_targets.empty() && ObjectArrayReadable() && VmReady();
}

// The push itself, shared by the gameplay and suppressed paths. Only the walk
// that finds the widgets differs between them.
void PushCurrentOffset() {
    if (!g_setRenderTranslationFn) return;

    float dx = 0.0f, dy = 0.0f;
    const bool haveOffset = AimProjection::GetScreenOffset(dx, dy);
    // No valid offset means tracking is off, suppressed, or the aim is behind
    // the view. Park the crosshair back at the centre rather than leaving it
    // stuck wherever the last tracked frame put it.
    const float slateX = haveOffset ? dx / g_dpiScale : 0.0f;
    const float slateY = haveOffset ? dy / g_dpiScale : 0.0f;
    if (!NeedsPush(slateX, slateY)) return;

    for (Widget& w : g_widgets) Push(w, slateX, slateY);

    LogOffset(haveOffset, dx, dy, slateX, slateY);
}

}  // namespace

void SetUncappedLog(bool on) { g_uncappedLog = on; }

void Initialize() {
    g_targets = ParseTargets(kCrosshairWidgets);
    g_widgets.assign(g_targets.size(), Widget{});
    g_nameIds.assign(g_targets.size(), 0u);
    for (const Target& t : g_targets)
        Log::Line("reticle: target %s%s%s", t.Name.c_str(),
            t.Outer.empty() ? "" : " under ", t.Outer.c_str());
}

bool Holding() {
    for (const Widget& w : g_widgets) if (Live(w)) return true;
    return false;
}

std::size_t TargetCount() { return g_targets.size(); }

void Tick() {
    if (!CanPush()) return;
    MaybeRefreshTargets();
    PushCurrentOffset();
}

void Park() {
    if (!CanPush()) return;
    PushCurrentOffset();
}

}  // namespace tow_ht::ReticleMover

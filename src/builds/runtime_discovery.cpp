// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "runtime_discovery.h"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

namespace tow_ht::builds {
namespace {

class Rejected : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void Require(bool condition, const char* reason) {
    if (!condition) throw Rejected(reason);
}

struct Range {
    std::uint32_t begin;
    std::uint32_t size;

    bool Contains(std::uint64_t at, std::size_t length = 1) const {
        return at >= begin && length <= size && at - begin <= size - length;
    }
};

class Image {
public:
    explicit Image(ImageView view) : view_(view) {
        Require(Read<std::uint16_t>(0) == 0x5a4d, "missing DOS header");
        const auto nt = Read<std::uint32_t>(0x3c);
        Require(Read<std::uint32_t>(nt) == 0x4550 &&
                Read<std::uint16_t>(nt + 4) == 0x8664 &&
                Read<std::uint16_t>(nt + 24) == 0x20b, "expected an x64 PE image");
        Require(Read<std::uint32_t>(nt + 24 + 56) == view.size,
                "PE image size disagrees with the captured module");
        const auto count = Read<std::uint16_t>(nt + 6);
        const auto optionalSize = Read<std::uint16_t>(nt + 20);
        Require(optionalSize >= 160 && count > 0 && count <= 96, "invalid PE section table");
        const std::uint64_t sections = static_cast<std::uint64_t>(nt) + 24 + optionalSize;
        for (unsigned i = 0; i < count; ++i) {
            const auto header = sections + i * 40;
            Bounds(header, 40);
            char name[9]{};
            std::memcpy(name, view.data + header, 8);
            Range range{Read<std::uint32_t>(header + 12), Read<std::uint32_t>(header + 8)};
            Bounds(range.begin, range.size);
            Require(sections_.emplace(name, range).second, "duplicate PE section name");
            flags_.emplace(name, Read<std::uint32_t>(header + 36));
        }
        text = Section(".text", 0x20000000);
        rdata = Section(".rdata", 0x40000000);
        writable = Section(".data", 0x80000000);
        const auto pdata = Section(".pdata", 0x40000000);
        const auto exceptionRva = Read<std::uint32_t>(nt + 24 + 112 + 3 * 8);
        const auto exceptionSize = Read<std::uint32_t>(nt + 24 + 112 + 3 * 8 + 4);
        Require(exceptionSize && exceptionSize % 12 == 0 &&
                pdata.Contains(exceptionRva, exceptionSize), "invalid PE exception directory");
        for (std::uint32_t i = 0; i < exceptionSize; i += 12) {
            const auto begin = Read<std::uint32_t>(exceptionRva + i);
            const auto end = Read<std::uint32_t>(exceptionRva + i + 4);
            Require(end > begin && end <= view.size, "invalid function extent");
            Require(functions_.empty() || functions_.back().begin < begin,
                    "unsorted PE exception directory");
            functions_.push_back({begin, end - begin});
        }
    }

    template<class T> T Read(std::uint64_t at) const {
        Bounds(at, sizeof(T));
        T value;
        std::memcpy(&value, view_.data + at, sizeof(T));
        return value;
    }

    std::uint32_t Pointer(std::uint32_t at) const {
        const auto value = Read<std::uint64_t>(at);
        if (value < view_.base || value - view_.base >= view_.size) return 0;
        return static_cast<std::uint32_t>(value - view_.base);
    }

    std::uint32_t Relative(std::uint32_t displacement) const {
        const auto value = static_cast<std::int64_t>(displacement) + 4 + Read<std::int32_t>(displacement);
        Require(value >= 0 && static_cast<std::uint64_t>(value) < view_.size,
                "relative instruction target is outside the module");
        return static_cast<std::uint32_t>(value);
    }

    Range Function(std::uint32_t at) const {
        auto it = std::upper_bound(functions_.begin(), functions_.end(), at,
            [](std::uint32_t address, Range range) { return address < range.begin; });
        if (it == functions_.begin() || !(--it)->Contains(at)) return {};
        return *it;
    }

    std::vector<std::uint32_t> Find(Range range, std::initializer_list<int> pattern) const {
        std::vector<std::uint32_t> hits;
        if (pattern.size() > range.size) return hits;
        const auto last = range.begin + range.size - pattern.size();
        for (std::uint32_t at = range.begin; at <= last; ++at) {
            std::size_t index = 0;
            for (const int byte : pattern) {
                if (byte >= 0 && view_.data[at + index] != byte) break;
                ++index;
            }
            if (index == pattern.size()) hits.push_back(at);
        }
        return hits;
    }

    std::vector<std::uint32_t> Strings(Range range, const char* name, bool wide = false) const {
        std::vector<std::uint8_t> bytes;
        do {
            bytes.push_back(static_cast<std::uint8_t>(*name));
            if (wide) bytes.push_back(0);
        } while (*name++);
        std::vector<std::uint32_t> hits;
        auto cursor = view_.data + range.begin;
        const auto end = cursor + range.size;
        while (cursor < end) {
            const auto found = std::search(cursor, end, bytes.begin(), bytes.end());
            if (found == end) break;
            const auto at = static_cast<std::uint32_t>(found - view_.data);
            if (at == range.begin || Read<std::uint8_t>(at - 1) == 0) hits.push_back(at);
            cursor = found + 1;
        }
        return hits;
    }

    std::vector<std::uint32_t> Pointers(std::uint32_t target) const {
        std::vector<std::uint32_t> hits;
        for (std::uint32_t at = rdata.begin; rdata.Contains(at, 8); at += 8)
            if (Read<std::uint64_t>(at) == view_.base + target) hits.push_back(at);
        return hits;
    }

    std::vector<std::uint32_t> Vtable(const char* name) const {
        const auto names = Strings(writable, name);
        Require(names.size() == 1 && names[0] >= writable.begin + 16, "RTTI type name is missing or ambiguous");
        const auto descriptor = names[0] - 16;
        std::vector<std::uint32_t> tables;
        for (auto at = rdata.begin; rdata.Contains(at, 24); at += 4) {
            if (Read<std::uint32_t>(at) != 1 || Read<std::uint32_t>(at + 4) != 0 ||
                Read<std::uint32_t>(at + 12) != descriptor || Read<std::uint32_t>(at + 20) != at) continue;
            for (const auto pointer : Pointers(at))
                if (rdata.Contains(pointer + 8, 8) && text.Contains(Pointer(pointer + 8))) tables.push_back(pointer + 8);
        }
        Require(tables.size() == 1, "primary RTTI vtable is missing or ambiguous");
        std::vector<std::uint32_t> functions;
        for (auto at = tables[0]; rdata.Contains(at, 8); at += 8) {
            const auto target = Pointer(at);
            if (!text.Contains(target)) break;
            functions.push_back(target);
        }
        return functions;
    }

    Range text{}, rdata{}, writable{};

private:
    void Bounds(std::uint64_t at, std::size_t length) const {
        Require(at <= view_.size && length <= view_.size - at, "read extends beyond the captured image");
    }

    Range Section(const char* name, std::uint32_t requiredFlag) const {
        const auto found = sections_.find(name);
        Require(found != sections_.end() && found->second.size != 0, "required PE section is missing");
        Require((flags_.at(name) & requiredFlag) != 0, "unexpected PE section protection");
        return found->second;
    }

    ImageView view_;
    std::map<std::string, Range> sections_;
    std::map<std::string, std::uint32_t> flags_;
    std::vector<Range> functions_;
};

std::uint32_t Unique(const std::set<std::uint32_t>& values, const char* reason) {
    Require(values.size() == 1, reason);
    return *values.begin();
}

std::uint32_t ViewSlot(const Image& image, const std::vector<std::uint32_t>& ai) {
    std::set<std::uint32_t> eyeSlots;
    for (const auto name : image.Strings(image.rdata, "GetActorEyesViewPoint")) {
        for (const auto record : image.Pointers(name)) {
            if (!image.rdata.Contains(record, 16)) continue;
            const auto function = image.Pointer(record + 8);
            if (!image.text.Contains(function)) continue;
            const auto extent = image.Function(function);
            for (const auto call : image.Find(extent, {0x49,0x8b,0x06,0xff,0x90,-1,-1,-1,-1})) {
                const auto displacement = image.Read<std::uint32_t>(call + 5);
                if (displacement % 8 == 0 && displacement / 8 < ai.size()) eyeSlots.insert(displacement);
            }
        }
    }
    const auto eye = Unique(eyeSlots, "GetActorEyesViewPoint native dispatch is missing or ambiguous");
    std::set<std::uint32_t> slots;
    for (std::uint32_t slot = 0; slot < ai.size(); ++slot) {
        if (!image.text.Contains(ai[slot], 10)) continue;
        const auto hits = image.Find({ai[slot], 10}, {0x48,0x8b,0x01,0x48,0xff,0xa0,-1,-1,-1,-1});
        if (!hits.empty() && image.Read<std::uint32_t>(ai[slot] + 6) == eye) slots.insert(slot);
    }
    return Unique(slots, "controller view-point forwarder is missing or ambiguous");
}

std::uint32_t CursorOffset(const Image& image) {
    std::set<std::uint32_t> offsets;
    unsigned mask = 2;
    for (const auto name : {"bEnableClickEvents", "bEnableTouchEvents", "bEnableMouseOverEvents"}) {
        std::set<std::uint32_t> matches;
        for (const auto string : image.Strings(image.rdata, name)) {
            for (const auto record : image.Pointers(string)) {
                if (!image.rdata.Contains(record, 0x38)) continue;
                const auto setter = image.Pointer(record + 0x30);
                if (!image.text.Contains(setter, 8)) continue;
                if (image.Find({setter, 8}, {0x83,0x89,-1,-1,-1,-1,static_cast<int>(mask),0xc3}).empty()) continue;
                const auto offset = image.Read<std::uint32_t>(setter + 2);
                const auto classSize = image.Read<std::uint32_t>(record + 0x28);
                if (offset >= 0x28 && classSize < 0x10000 && offset + 4 <= classSize) matches.insert(offset);
            }
        }
        offsets.insert(Unique(matches, "player-controller cursor bitfield setter is missing or ambiguous"));
        mask <<= 1;
    }
    return Unique(offsets, "player-controller cursor bitfield setters disagree");
}

OffsetTable Resolve(const Image& image) {
    OffsetTable out{};
    const auto player = image.Vtable(".?AVAIndianaPlayerController@@");
    const auto ai = image.Vtable(".?AVAIndianaAiController@@");
    const auto slot = ViewSlot(image, ai);
    Require(slot < player.size(), "player-controller view-point slot is absent");
    out.kGetPlayerViewPointRva = player[slot];
    const auto view = image.Function(player[slot]);
    Require(view.begin == player[slot], "view-point target has no function entry");
    Require(!image.Find(view, {0xf2,0x0f,0x10,0x83,0xb0,0x03,0,0,0xf2,0x0f,0x11,0x06}).empty() &&
            !image.Find(view, {0xf2,0x0f,0x10,0x83,0xbc,0x03,0,0,0xf2,0x41,0x0f,0x11,0x06}).empty(),
            "view-point function does not contain the UE4 cached position and rotation copies");

    std::set<std::uint32_t> render;
    for (const auto at : image.Find(image.text, {
             0x48,0x8b,0x88,0x50,0x03,0,0,0x48,0x8b,0x01,0xff,0x90,-1,-1,-1,-1,
             0xf3,0x0f,0x11,0x47,0x18,0x48,0x8b,0x4d,0x38,0x48,0x8b,0x01,
             0x4c,0x8d,0x47,0x0c,0x48,0x8b,0xd7,0xff,0x90,-1,-1,-1,-1})) {
        if (image.Read<std::uint32_t>(at + 37) != slot * 8) continue;
        const auto function = image.Function(at);
        if (!function.Contains(at, 41)) continue;
        if (image.Find(function, {0x8b,0x43,0x2c,0x89,0x47,0x2c}).empty()) continue;
        render.insert(at + 41);
    }
    out.kKnownCallerRvas[0] = Unique(render, "render/projection view-point caller is missing or ambiguous");
    out.kDefaultInjectMode = inject::kFirstCaller;
    out.kShowMouseCursorOffset = CursorOffset(image);
    out.kShowMouseCursorMask = 1;
    out.MinimalViewInfoLayout = {0x18, 0x0c, 0x2c};

    std::set<std::uint32_t> events;
    for (const auto at : image.Find(image.text, {
             0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x57,0x48,0x83,0xec,0x20,
             0xf7,0x82,0xb8,0,0,0,0,0x04,0,0})) {
        if (image.Function(at).begin == at && std::find(player.begin(), player.end(), at) != player.end()) events.insert(at);
    }
    out.kProcessEventRva = Unique(events, "player-controller ProcessEvent entry is missing or ambiguous");

    auto leas = image.Find(image.text, {0x48,0x8d,-1,-1,-1,-1,-1});
    const auto extendedLeas = image.Find(image.text, {0x4c,0x8d,-1,-1,-1,-1,-1});
    leas.insert(leas.end(), extendedLeas.begin(), extendedLeas.end());
    std::map<std::uint32_t, std::set<std::uint32_t>> leaTargets;
    for (const auto at : leas) {
        if ((image.Read<std::uint8_t>(at + 2) & 0xc7) != 5) continue;
        const auto target = static_cast<std::int64_t>(at) + 7 + image.Read<std::int32_t>(at + 3);
        if (target >= 0 && target <= UINT32_MAX) leaTargets[static_cast<std::uint32_t>(target)].insert(at);
    }
    std::set<std::uint32_t> allocators;
    for (const auto string : image.Strings(image.rdata, "Unable to add more objects to disregard for GC pool (Max: %d)", true)) {
        for (const auto at : leaTargets[string]) {
            const auto fn = image.Function(at);
            if (fn.size) allocators.insert(fn.begin);
        }
    }
    const auto allocator = Unique(allocators, "object-array allocator string reference is missing or ambiguous");
    std::set<std::uint32_t> arrays;
    for (const auto at : image.Find(image.text, {0x45,0x33,0xc0,0x48,0x8d,0x0d,-1,-1,-1,-1,0x48,0x8b,0xd7,0xe8,-1,-1,-1,-1})) {
        if (image.Relative(at + 14) != allocator) continue;
        const auto array = image.Relative(at + 6);
        if (image.writable.Contains(array, 0x30)) arrays.insert(array + 0x10);
    }
    const auto objects = Unique(arrays, "object-array allocator receiver is missing or ambiguous");

    std::map<std::uint32_t, unsigned> constructors;
    for (const auto name : {"ByteProperty", "IntProperty", "BoolProperty", "ObjectProperty", "FloatProperty", "StructProperty", "NameProperty"}) {
        std::set<std::uint32_t> functions;
        for (const auto string : image.Strings(image.rdata, name)) {
            for (const auto at : leaTargets[string]) {
                const auto fn = image.Function(at);
                if (fn.size) functions.insert(fn.begin);
            }
        }
        for (const auto fn : functions) ++constructors[fn];
    }
    std::set<std::uint32_t> nameConstructors;
    for (const auto& entry : constructors) if (entry.second == 7) nameConstructors.insert(entry.first);
    const auto constructor = Unique(nameConstructors, "name-pool constructor does not reference all seven property names uniquely");
    std::set<std::uint32_t> pools;
    for (const auto at : image.Find(image.text, {0x48,0x8d,0x0d,-1,-1,-1,-1,0xe8,-1,-1,-1,-1})) {
        if (image.Relative(at + 8) != constructor) continue;
        const auto pool = image.Relative(at + 3);
        if (image.writable.Contains(pool, 0x10010)) pools.insert(pool);
    }
    const auto pool = Unique(pools, "name-pool constructor receiver is missing or ambiguous");
    out.UObjectGlobals = {objects, 0x14, 0x18, 0x10000, pool, 0x10, 0x10, 0x18, 0x20};
    out.Reflection = {0x08, 0x20, 0x28, 0x38, 0x3c, 0x4c, 0x00, 0x48, 0x58, 0x60};
    return out;
}

}

bool DiscoverOffsets(ImageView image, OffsetTable& offsets, std::string& reason) {
    try {
        offsets = Resolve(Image(image));
        reason.clear();
        return true;
    } catch (const Rejected& error) {
        reason = error.what();
        return false;
    }
}

}

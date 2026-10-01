// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "builds/runtime_discovery.h"
#include "test_harness.h"

#include <cstring>
#include <initializer_list>
#include <vector>

namespace {

struct Fixture {
    std::uint32_t shift;
    std::uintptr_t base;
    std::vector<std::uint8_t> bytes;

    template<class T> void Write(std::uint32_t at, T value) {
        std::memcpy(bytes.data() + at, &value, sizeof(value));
    }

    void Code(std::uint32_t at, std::initializer_list<std::uint8_t> code) {
        std::copy(code.begin(), code.end(), bytes.begin() + at + shift);
    }

    void String(std::uint32_t at, const char* value, bool wide = false) {
        do {
            bytes[at++ + shift] = static_cast<std::uint8_t>(*value);
            if (wide) bytes[at++ + shift] = 0;
        } while (*value++);
    }

    void Pointer(std::uint32_t at, std::uint32_t target) {
        Write<std::uint64_t>(at + shift, base + shift + target);
    }

    void Relative(std::uint32_t at, std::uint32_t target) {
        Write<std::int32_t>(at + shift, static_cast<std::int32_t>(target) - static_cast<std::int32_t>(at) - 4);
    }

    void Table(std::uint32_t name, const char* text, std::uint32_t col, std::uint32_t table) {
        String(name, text);
        Write<std::uint32_t>(col + shift, 1);
        Write<std::uint32_t>(col + 12 + shift, name + shift - 16);
        Write<std::uint32_t>(col + 20 + shift, col + shift);
        Pointer(table - 8, col);
        for (unsigned i = 0; i < 12; ++i) Pointer(table + i * 8, 0x1100);
    }

    Fixture(std::uint32_t displacement = 0, std::uintptr_t imageBase = 0x140000000,
            unsigned eyes = 5, unsigned view = 8)
        : shift(displacement), base(imageBase), bytes(0x30000 + shift) {
        Write<std::uint16_t>(0, 0x5a4d);
        Write<std::uint32_t>(0x3c, 0x80);
        Write<std::uint32_t>(0x80, 0x4550);
        Write<std::uint16_t>(0x84, 0x8664);
        Write<std::uint16_t>(0x86, 4);
        Write<std::uint16_t>(0x94, 240);
        Write<std::uint16_t>(0x98, 0x20b);
        Write<std::uint32_t>(0x98 + 56, static_cast<std::uint32_t>(bytes.size()));
        struct Section { const char* name; std::uint32_t rva, size, flags; };
        const Section sections[] = {
            {".text", 0x1000, 0x2000, 0x60000000},
            {".rdata", 0x4000, 0x3000, 0x40000000},
            {".data", 0x8000, 0x20000, 0xc0000000},
            {".pdata", 0x29000, 0x1000, 0x40000000},
        };
        std::uint32_t header = 0x188;
        for (const auto& section : sections) {
            std::memcpy(bytes.data() + header, section.name, std::strlen(section.name));
            Write<std::uint32_t>(header + 8, section.size);
            Write<std::uint32_t>(header + 12, section.rva + shift);
            Write<std::uint32_t>(header + 36, section.flags);
            header += 40;
        }
        const std::uint32_t functions[][2] = {
            {0x1200,0x1220}, {0x1300,0x1310}, {0x1400,0x1480}, {0x1700,0x1780},
            {0x1800,0x1830}, {0x1900,0x1908}, {0x1910,0x1918}, {0x1920,0x1928},
            {0x1a00,0x1a40}, {0x1b00,0x1b40}, {0x1c00,0x1c80}, {0x1d00,0x1d20},
        };
        std::uint32_t record = 0x29000 + shift;
        for (const auto& function : functions) {
            Write<std::uint32_t>(record, function[0] + shift);
            Write<std::uint32_t>(record + 4, function[1] + shift);
            record += 12;
        }
        Write<std::uint32_t>(0x98 + 112 + 24, 0x29000 + shift);
        Write<std::uint32_t>(0x98 + 112 + 28, record - 0x29000 - shift);
        Table(0x8020, ".?AVAIndianaPlayerController@@", 0x4c00, 0x4d08);
        Table(0x80a0, ".?AVAIndianaAiController@@", 0x4c40, 0x4e08);
        Pointer(0x4d08 + view * 8, 0x1400);
        Pointer(0x4d08 + 2 * 8, 0x1800);
        Pointer(0x4e08 + view * 8, 0x1300);
        String(0x4a00, "GetActorEyesViewPoint");
        Pointer(0x4f00, 0x4a00);
        Pointer(0x4f08, 0x1200);
        Code(0x1200, {0x49,0x8b,0x06,0xff,0x90,0,0,0,0});
        Write<std::uint32_t>(0x1205 + shift, eyes * 8);
        Code(0x1300, {0x48,0x8b,0x01,0x48,0xff,0xa0,0,0,0,0});
        Write<std::uint32_t>(0x1306 + shift, eyes * 8);
        Code(0x1400, {0xf2,0x0f,0x10,0x83,0xb0,0x03,0,0,0xf2,0x0f,0x11,0x06});
        Code(0x1420, {0xf2,0x0f,0x10,0x83,0xbc,0x03,0,0,0xf2,0x41,0x0f,0x11,0x06});
        Code(0x1700, {0x8b,0x43,0x2c,0x89,0x47,0x2c});
        Code(0x1710, {0x48,0x8b,0x88,0x50,0x03,0,0,0x48,0x8b,0x01,0xff,0x90,0xc8,7,0,0,
                     0xf3,0x0f,0x11,0x47,0x18,0x48,0x8b,0x4d,0x38,0x48,0x8b,0x01,
                     0x4c,0x8d,0x47,0x0c,0x48,0x8b,0xd7,0xff,0x90,0,0,0,0});
        Write<std::uint32_t>(0x1710 + 37 + shift, view * 8);
        Code(0x1800, {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x57,0x48,0x83,0xec,0x20,
                     0xf7,0x82,0xb8,0,0,0,0,0x04,0,0});
        unsigned index = 0;
        for (const auto name : {"bEnableClickEvents", "bEnableTouchEvents", "bEnableMouseOverEvents"}) {
            const auto property = 0x5000 + index * 0x100;
            String(0x5300 + index * 0x40, name);
            Pointer(property, 0x5300 + index * 0x40);
            Pointer(property + 0x30, 0x1900 + index * 0x10);
            Write<std::uint32_t>(property + 0x28 + shift, 0x628);
            Code(0x1900 + index * 0x10, {0x83,0x89,0xe0,4,0,0,static_cast<std::uint8_t>(2 << index),0xc3});
            ++index;
        }
        String(0x4800, "Unable to add more objects to disregard for GC pool (Max: %d)", true);
        Code(0x1a00, {0x4c,0x8d,0x05,0,0,0,0});
        Relative(0x1a03, 0x4800);
        Code(0x1b00, {0x45,0x33,0xc0,0x48,0x8d,0x0d,0,0,0,0,0x48,0x8b,0xd7,0xe8,0,0,0,0});
        Relative(0x1b06, 0x9000);
        Relative(0x1b0e, 0x1a00);
        index = 0;
        for (const auto name : {"ByteProperty", "IntProperty", "BoolProperty", "ObjectProperty", "FloatProperty", "StructProperty", "NameProperty"}) {
            String(0x5400 + index * 0x40, name);
            Code(0x1c00 + index * 0x10, {0x48,0x8d,0x15,0,0,0,0});
            Relative(0x1c03 + index * 0x10, 0x5400 + index * 0x40);
            ++index;
        }
        Code(0x1d00, {0x48,0x8d,0x0d,0,0,0,0,0xe8,0,0,0,0});
        Relative(0x1d03, 0x10000);
        Relative(0x1d08, 0x1c00);
    }

    bool Discover(tow_ht::OffsetTable& out, std::string& reason) const {
        return tow_ht::builds::DiscoverOffsets({bytes.data(), bytes.size(), base}, out, reason);
    }
};

void Valid(const Fixture& fixture) {
    tow_ht::OffsetTable out{};
    std::string reason;
    CHECK_MSG(fixture.Discover(out, reason), reason.c_str());
    CHECK(out.kGetPlayerViewPointRva == 0x1400 + fixture.shift);
    CHECK(out.kKnownCallerRvas[0] == 0x1739 + fixture.shift);
    CHECK(out.kProcessEventRva == 0x1800 + fixture.shift);
    CHECK(out.UObjectGlobals.kObjObjects == 0x9010 + fixture.shift);
    CHECK(out.UObjectGlobals.kFNamePool == 0x10000 + fixture.shift);
    CHECK(out.kShowMouseCursorOffset == 0x4e0);
}

void Rejected(const Fixture& fixture, const char* diagnostic) {
    tow_ht::OffsetTable out{};
    out.kGetPlayerViewPointRva = 123;
    std::string reason;
    CHECK(!fixture.Discover(out, reason));
    CHECK_MSG(reason.find(diagnostic) != std::string::npos, reason.c_str());
    CHECK(out.kGetPlayerViewPointRva == 123);
}

}

void SceneDiscovery() {
    for (const auto shift : {0u, 0x5000u}) {
        Fixture f(shift, 0x7ff600000000);
        const std::uint32_t entries[][2] = {
            {0x2000,0x2080}, {0x2080,0x2100}, {0x2100,0x2180},
            {0x2200,0x2280}, {0x2300,0x2380}, {0x2400,0x2480}};
        auto record = 0x29000u + shift;
        for (const auto& entry : entries) {
            f.Write<std::uint32_t>(record, entry[0] + shift);
            f.Write<std::uint32_t>(record + 4, entry[1] + shift);
            f.Write<std::uint32_t>(record + 8, 0x6000 + shift);
            record += 12;
        }
        f.Write<std::uint32_t>(0x98 + 112 + 28, record - 0x29000 - shift);
        f.bytes[0x6000 + shift] = 1;
        f.bytes[0x6020 + shift] = 0x21;
        f.Write<std::uint32_t>(0x6024 + shift, 0x2000 + shift);
        f.Write<std::uint32_t>(0x6028 + shift, 0x2080 + shift);
        f.Write<std::uint32_t>(0x602c + shift, 0x6000 + shift);
        f.Write<std::uint32_t>(0x29000 + 12 + 8 + shift, 0x6020 + shift);
        f.Pointer(0x5800 + 8 * 5, 0x2000);
        f.Pointer(0x5800 + 8 * 6, 0x2100);
        f.Pointer(0x5800 + 8 * 9, 0x2300);
        f.Code(0x2110, {0xc7,0x85,0,0,0,0,0,0,0xb4,0x42,
            0xc7,0x85,0,0,0,0,0,0,0xb4,0x42,0x44,0x89,0x64,0x24,0x20,0xe8,0,0,0,0});
        f.Relative(0x212a, 0x2200);
        f.Code(0x2210, {0x49,0x8b,0xd6,0x45,0x8b,0xc4,0xff,0x90,0x48,0,0,0});
        f.Code(0x2310, {0xff,0x90,0x28,0,0,0});
        std::uint32_t scene = 0;
        std::string reason;
        const auto discover = [&] {
            return tow_ht::builds::DiscoverSceneView(
                {f.bytes.data(), f.bytes.size(), f.base}, 0x2090 + shift, scene, reason);
        };
        CHECK_MSG(discover(), reason.c_str());
        CHECK(scene == 0x2100 + shift);
        f.bytes[0x2312 + shift] = 0x30;
        CHECK(!discover());
        f.bytes[0x2312 + shift] = 0x28;
        f.Pointer(0x5900 + 8 * 5, 0x2000);
        f.Pointer(0x5900 + 8 * 6, 0x2400);
        f.Pointer(0x5900 + 8 * 9, 0x2300);
        std::memcpy(f.bytes.data() + 0x2410 + shift, f.bytes.data() + 0x2110 + shift, 30);
        f.Relative(0x242a, 0x2200);
        CHECK(!discover());
        CHECK(reason.find("uniquely") != std::string::npos);
        f.Write<std::uint32_t>(0x602c + shift, 0x6020 + shift);
        CHECK(!discover());
        CHECK(reason.find("unwind chain") != std::string::npos);
    }
}

int main() {
    SceneDiscovery();
    Valid(Fixture{});
    Valid(Fixture{0x5000, 0x7ff600000000, 7, 10});
    Fixture duplicate;
    std::memcpy(duplicate.bytes.data() + 0x1740, duplicate.bytes.data() + 0x1710, 41);
    Rejected(duplicate, "render/projection");
    Fixture wrongSlot;
    wrongSlot.Write<std::uint32_t>(0x1735, 0x88);
    Rejected(wrongSlot, "render/projection");
    Fixture wideRotation;
    wideRotation.bytes[0x1710 + 31] = 0x18;
    Rejected(wideRotation, "render/projection");
    Fixture wrongCursor;
    wrongCursor.Write<std::uint32_t>(0x1912, 0x4e4);
    Rejected(wrongCursor, "setters disagree");
    Fixture missingName;
    missingName.bytes[0x5400] = 'X';
    Rejected(missingName, "name-pool constructor");
    Fixture wrongGlobal;
    wrongGlobal.Relative(0x1d03, 0x1100);
    Rejected(wrongGlobal, "name-pool constructor receiver");
    Fixture wrongEvent;
    wrongEvent.Pointer(0x4d18, 0x1100);
    Rejected(wrongEvent, "ProcessEvent");
    Fixture truncated;
    truncated.bytes.resize(0x100);
    Rejected(truncated, "size disagrees");
    Fixture badHeader;
    badHeader.Write<std::uint32_t>(0x3c, 0xfffffff0);
    Rejected(badHeader, "beyond");
    Fixture wrongBase;
    wrongBase.base += 0x100000;
    Rejected(wrongBase, "vtable");
    return tow_test::Report();
}

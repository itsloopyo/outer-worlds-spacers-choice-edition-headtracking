// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// What HeadTracking.ini is allowed to say.
//
// The ini is a user-editable file, so it is a system boundary, and two of the
// readers behind it hand back something that looks valid for input that is not:
// strtod accepts "nan" and "inf" as floats, and GetPrivateProfileIntA answers 0
// - not the default - for a key that is present but unparseable. Neither
// failure is visible downstream. A NaN sensitivity comes out of the processor
// as a NaN FRotator and is written into engine memory through the camera hook;
// a zeroed [Diag] InjectMode is the diagnostic mode that hands every
// GetPlayerViewPoint caller the head pose, which is the aim decoupling switched
// off.
//
// So what this suite locks is that a non-finite value never survives, that a
// value outside its range lands somewhere deliberate, and that a valid value -
// including a deliberate 0.0 - is returned exactly as written.

#include "legacy_config/config_sanitize.h"
#include "test_harness.h"

#include <limits>

namespace {

namespace cs = tow_ht::config_sanitize;

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

void TestFloatPassesValidValuesThrough() {
    CHECK_NEAR(cs::Float(0.5f, 0.15f, 0.0f, 1.0f), 0.5f, 0.0f);
    // The whole reason this is validation and not a floor: LocalSmoothing
    // defaults to 0.0 and a player who sets it to 0.0 means it.
    CHECK_NEAR_MSG(cs::Float(0.0f, 0.15f, 0.0f, 1.0f), 0.0f, 0.0f,
                   "a deliberately configured 0.0 stays 0.0");
    CHECK_NEAR(cs::Float(1.0f, 0.15f, 0.0f, 1.0f), 1.0f, 0.0f);
}

void TestFloatRejectsNonFinite() {
    // std::clamp alone does NOT do this: both of its comparisons are false for
    // a NaN, so the NaN comes back out.
    CHECK_NEAR_MSG(cs::Float(kNaN, 0.15f, 0.0f, 1.0f), 0.15f, 0.0f,
                   "a NaN smoothing value falls back rather than passing through");
    CHECK_NEAR_MSG(cs::Float(kInf, 0.15f, 0.0f, 1.0f), 0.15f, 0.0f,
                   "+inf falls back");
    CHECK_NEAR_MSG(cs::Float(-kInf, 0.15f, 0.0f, 1.0f), 0.15f, 0.0f,
                   "-inf falls back");
    // A NaN sensitivity is the one that reaches the camera: it multiplies the
    // pose, survives the processor's clamp, and lands in the FVector written
    // through the hook.
    CHECK_MSG(cs::Float(kNaN, 1.0f, 0.1f, 3.0f) == 1.0f,
              "a NaN sensitivity never reaches the camera");
}

void TestFloatClampsOutOfRange() {
    CHECK_NEAR(cs::Float(-0.5f, 0.15f, 0.0f, 1.0f), 0.0f, 0.0f);
    CHECK_NEAR(cs::Float(7.0f, 0.15f, 0.0f, 1.0f), 1.0f, 0.0f);
    // A lean limit past the documented half-metre is clamped, not dropped: the
    // player asked for more travel and gets as much as the mod will give.
    CHECK_NEAR(cs::Float(0.9f, 0.30f, 0.01f, 0.5f), 0.5f, 0.0f);
}

void TestIntFallsBackRatherThanClamping() {
    CHECK(cs::Int(4242, 4242, 1024, 65535) == 4242);
    CHECK(cs::Int(1024, 4242, 1024, 65535) == 1024);
    CHECK(cs::Int(65535, 4242, 1024, 65535) == 65535);
    // 70000 pinned to 65535 would be as wrong as the 4464 the uint16_t cast
    // makes of it. The default is at least the port the tracker is sending to.
    CHECK_MSG(cs::Int(70000, 4242, 1024, 65535) == 4242,
              "an out-of-range UDP port falls back to the default, not to the bound");
    CHECK(cs::Int(0, 4242, 1024, 65535) == 4242);
    CHECK(cs::Int(-1, 4242, 1024, 65535) == 4242);
    // A trace channel is written into a Kismet frame as one byte, so 300 would
    // silently become 44.
    CHECK_MSG(cs::Int(300, 0, 0, 255) == 0,
              "a trace channel wider than a byte falls back rather than truncating");
    // Virtual keys: 0 is "no key" and 0xFF is the OEM catch-all.
    CHECK(cs::Int(0x22, 0x22, 0x01, 0xFE) == 0x22);
    CHECK(cs::Int(0x00, 0x22, 0x01, 0xFE) == 0x22);
    CHECK(cs::Int(0xFF, 0x22, 0x01, 0xFE) == 0x22);
}

void TestParseIntSeparatesGarbageFromZero() {
    int out = -999;
    CHECK(cs::ParseInt("4242", out) && out == 4242);
    out = -999;
    CHECK_MSG(cs::ParseInt("0", out) && out == 0,
              "a written 0 parses as 0, unlike the 0 ReadInt invents for garbage");
    out = -999;
    CHECK(cs::ParseInt("-1", out) && out == -1);
    // Windows does not treat ';' as an inline comment introducer, so the whole
    // line arrives and the prefix has to parse.
    out = -999;
    CHECK(cs::ParseInt("4242 ; the port", out) && out == 4242);
    // Base 10, not strtoll's base-0 auto-detect: 0755 is what the player typed.
    out = -999;
    CHECK_MSG(cs::ParseInt("0755", out) && out == 755,
              "a leading zero is not an octal prefix");

    out = -999;
    CHECK_MSG(!cs::ParseInt("", out), "an absent key leaves the default alone");
    CHECK(out == -999);
    CHECK_MSG(!cs::ParseInt("banana", out),
              "garbage is reported as garbage rather than read back as 0");
    CHECK(out == -999);
    CHECK_MSG(!cs::ParseInt("99999999999999999999", out),
              "a value too wide for an int is rejected, not saturated");
    CHECK(out == -999);
}

// The bool boundary. IniReader::ReadBool answers the caller's DEFAULT for
// anything outside its allow-list and says nothing, so this parse is what stands
// between a hand-edited ini and a setting that silently reads back as its
// opposite.
void TestParseBoolAcceptsEverySpellingTheIniHasEverAccepted() {
    bool out = false;
    for (const char* t : {"1", "true", "TRUE", "True", "yes", "YES", "on", "ON"}) {
        out = false;
        CHECK_MSG(tow_ht::config_sanitize::ParseBool(t, out) && out, t);
    }
    for (const char* f : {"0", "false", "FALSE", "False", "no", "NO", "off", "OFF"}) {
        out = true;
        CHECK_MSG(tow_ht::config_sanitize::ParseBool(f, out) && !out, f);
    }
}

// Windows does not treat ';' as an inline comment introducer, so the rest of the
// line arrives attached to the value. Both spacings have to read the same, and
// the same way the integer keys already do.
void TestParseBoolStopsWhereParseIntStops() {
    bool out = false;
    CHECK(tow_ht::config_sanitize::ParseBool("1 ; turn collision on", out) && out);
    out = false;
    CHECK_MSG(tow_ht::config_sanitize::ParseBool("1;turn collision on", out) && out,
              "no space before the comment reads the same as with one");
    out = true;
    CHECK(tow_ht::config_sanitize::ParseBool("  off   ", out) && !out);

    int n = 0;
    CHECK_MSG(tow_ht::config_sanitize::ParseInt("4242 ; the port", n) && n == 4242,
              "which is what the integer keys already do");
}

// A value it does not understand leaves `out` alone, so the caller can tell it
// apart from a value that says false and report it.
void TestParseBoolRejectsWhatItDoesNotUnderstand() {
    bool out = true;
    CHECK_MSG(!tow_ht::config_sanitize::ParseBool("maybe", out) && out,
              "an unrecognised word leaves the fallback in place");
    CHECK(!tow_ht::config_sanitize::ParseBool("", out));
    CHECK(!tow_ht::config_sanitize::ParseBool("   ", out));
    CHECK(!tow_ht::config_sanitize::ParseBool(";1", out));
    CHECK_MSG(out, "none of those touched it");
}

}  // namespace

int main() {
    TestFloatPassesValidValuesThrough();
    TestFloatRejectsNonFinite();
    TestFloatClampsOutOfRange();
    TestIntFallsBackRatherThanClamping();
    TestParseIntSeparatesGarbageFromZero();
    TestParseBoolAcceptsEverySpellingTheIniHasEverAccepted();
    TestParseBoolStopsWhereParseIntStops();
    TestParseBoolRejectsWhatItDoesNotUnderstand();
    return tow_test::Report();
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cctype>
#include <climits>
#include <cstdlib>
#include <string>

#include <locale.h>

#include "cameraunlock/math/finite_utils.h"

// What the ini is allowed to say, and what happens to a value that says
// something else.
//
// HeadTracking.ini is a user-editable file, so it is a system boundary and
// everything crossing it is validated once, here, before it reaches anything
// that would rather trust its arguments. Two failure modes make that worth a
// file of its own:
//
//  - `strtod` behind IniReader::ReadFloat parses "nan" and "inf" as perfectly
//    valid floats, and a range test made of two comparisons does NOT reject a
//    NaN because both of them are false. core's own math::Clamp is exactly that
//    pair of comparisons, so a NaN sensitivity travels through the processor
//    untouched, comes out as a NaN FRotator and FVector, and is written into
//    engine memory through the GetPlayerViewPoint hook for the rest of the
//    session with nothing in the log to explain the dead camera.
//  - `GetPrivateProfileIntA` behind IniReader::ReadInt answers 0 - not the
//    default - for a key that is PRESENT but unparseable (ini_reader.h, rule
//    4). For [Diag] InjectMode that 0 is the diagnostic mode that hands every
//    GetPlayerViewPoint caller the head pose, which is the aim decoupling
//    switched off; for [Network] UdpPort it is a bind on an ephemeral port that
//    no tracker is sending to.
//
// Pure functions returning the value to use, with no logging of their own, so
// the answer is a function of the arguments and each call site names its own
// key in the line it writes.
namespace tow_ht::config_sanitize {

// Non-finite becomes `fallback`, then the result is clamped into [lo, hi].
//
// Validation, never a floor: a finite value already inside the range comes back
// untouched, so a deliberately configured 0.0 stays 0.0.
inline float Float(float value, float fallback, float lo, float hi) {
    return cameraunlock::math::SanitizeFinite(value, fallback, lo, hi);
}

// Outside [lo, hi] becomes `fallback`, rather than the nearest bound.
//
// Clamping is the wrong answer for these: a UDP port of 70000 pinned to 65535
// is as wrong as the 4464 the uint16_t cast would otherwise make of it, and a
// virtual-key code clamped to 0xFE binds the toggle to a key nobody has. The
// shipped default is at least the value the rest of the setup is built around.
inline int Int(int value, int fallback, int lo, int hi) {
    return (value >= lo && value <= hi) ? value : fallback;
}

// The ini's decimal point is `.` regardless of what locale the game has put the
// CRT in, so the numeric parses run against a private C locale rather than the
// process one. IniReader does the same for the same reason; this is a second
// copy because core's is a file-static.
//
// A null answer is not a usable fallback: the _l-suffixed CRT functions read it
// as "use the current locale", which is the exact behaviour this exists to
// avoid, silently. Callers refuse the parse instead.
inline _locale_t CNumericLocale() {
    static _locale_t loc = _create_locale(LC_NUMERIC, "C");
    return loc;
}

// Parse an integer the ini carried as text, so a present-but-unparseable value
// is distinguishable from a valid zero. False leaves `out` alone.
//
// A prefix parse, matching what ReadInt does with a trailing comment: Windows
// does not treat ';' as an inline comment introducer, so "4242 ; the port"
// arrives whole and has to read back as 4242.
//
// Base 10, not strtoll's base-0 auto-detect: a port written 0755 is decimal 755
// to everyone who typed it, and octal 493 to base 0. strtoll rather than strtol
// because long is 32 bits on this toolchain, which would make the range test
// below unreachable and let a saturated LONG_MAX read back as a valid int.
//
// What follows the digits decides whether the prefix was the whole value. A
// trailing comment is; a second numeric token is not. `0x22` is the case that
// matters: base 10 stops at the `x`, and without this test the parse would
// report SUCCESS with the value 0 - so neither the "not a whole number" line
// nor the range line fires, and [Diag] InjectMode=0x1 silently becomes
// kAllCallers, which is the aim decoupling switched off. The shipped ini itself
// teaches the 0x spelling on YawModeKey, so it is a spelling users copy.
inline bool ValueEndsHere(const char* end) {
    const unsigned char c = static_cast<unsigned char>(*end);
    if (c == 0 || c == ';' || c == '#') return true;
    return std::isspace(c) != 0;
}

inline bool ParseInt(const std::string& text, int& out) {
    if (text.empty()) return false;
    const char* begin = text.c_str();
    char* end = nullptr;
    const long long value = std::strtoll(begin, &end, 10);
    if (end == begin) return false;
    if (!ValueEndsHere(end)) return false;
    if (value < INT_MIN || value > INT_MAX) return false;
    out = static_cast<int>(value);
    return true;
}

// Parse a float the ini carried as text, so a present-but-unparseable value is
// distinguishable from a valid one. False leaves `out` alone.
//
// IniReader::ReadFloat is a PREFIX parse that returns the prefix, and the range
// check downstream only speaks up when it changes the value - so a key whose
// prefix parses to something already inside its range is accepted in silence.
// `RemoteSmoothing=0,15` is the one that bites: a decimal comma is what a German
// or French keyboard writes, `_strtod_l` under the C locale stops at the comma
// and yields 0.0, 0.0 is a legal smoothing value, and the player has lost all
// smoothing on a WiFi tracker with the startup line agreeing that it is 0.00.
// `YawSensitivity=banana` is the other half: no parse at all, so the compiled
// default comes back and nothing says the key was ignored.
//
// _strtod_l against the C locale, matching IniReader, so the ini means the same
// thing whatever locale the game has put the CRT in.
inline bool ParseFloat(const std::string& text, float& out) {
    if (text.empty()) return false;
    const _locale_t loc = CNumericLocale();
    if (loc == nullptr) return false;
    const char* begin = text.c_str();
    // _strtod_l reads 0x22 as hexadecimal float 34.0, so without this a key
    // written in the 0x spelling the shipped ini teaches on YawModeKey would be
    // silently reinterpreted rather than rejected - [Collision] Radius=0x12
    // becoming 18cm, inside its range, with nothing logged. ParseInt rejects
    // that spelling; the two have to agree.
    const char* digits = begin;
    if (*digits == '+' || *digits == '-') ++digits;
    if (digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) return false;
    char* end = nullptr;
    const double value = _strtod_l(begin, &end, loc);
    if (end == begin) return false;
    if (!ValueEndsHere(end)) return false;
    out = static_cast<float>(value);
    return true;
}

// The leading token of an ini value, lowercased: ASCII whitespace skipped, then
// characters taken until the first that is neither a letter nor a digit.
//
// Same shape as ParseInt's prefix parse, and for the same reason - Windows does
// not treat ';' as an inline comment introducer, so `Enabled=1 ; on` and
// `Enabled=1;on` both arrive with the comment attached to the value. Stopping on
// the first non-alphanumeric character means both read as `1`, which is what
// strtoll already does for the integer keys; splitting on whitespace alone would
// have accepted one spelling and rejected the other in the same file.
inline std::string LeadingToken(const std::string& text) {
    std::string token;
    std::size_t i = 0;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
    for (; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (!std::isalnum(c)) break;
        token += static_cast<char>(std::tolower(c));
    }
    return token;
}

// Parse a boolean the ini carried as text. False leaves `out` alone, so a caller
// can tell a value it does not understand from one that says false.
//
// IniReader::ReadBool answers the caller's DEFAULT for anything outside its
// allow-list and says nothing, which is the same trap ParseInt exists to avoid:
// a player who writes `Enabled=yes ; lean collision on` gets the previous
// value back and a log that agrees with it.
inline bool ParseBool(const std::string& text, bool& out) {
    const std::string token = LeadingToken(text);
    if (token == "1" || token == "true" || token == "yes" || token == "on") {
        out = true;
        return true;
    }
    if (token == "0" || token == "false" || token == "no" || token == "off") {
        out = false;
        return true;
    }
    return false;
}

}  // namespace tow_ht::config_sanitize

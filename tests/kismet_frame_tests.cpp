// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The check that stands between a wrong reflection offset and a stack
// overflow.
//
// Both Kismet traces build their parameter frame in a fixed stack buffer at
// offsets read out of the engine's own reflection data. Proving a named field
// exists is not the same claim as proving its slot can hold the fixed-size C++
// type about to be memcpy'd there, and a slot narrower than the type runs the
// tail of that write off the end of the buffer. This is that second claim.

#include "kismet_frame.h"
#include "test_harness.h"

#include <limits>

namespace {

namespace kf = tow_ht::kismet_frame;
using tow_ht::ue_reflect::FieldInfo;

FieldInfo At(std::size_t offset, std::size_t size) {
    FieldInfo f;
    f.Offset = offset;
    f.Size = size;
    return f;
}

void TestFieldFitsAcceptsASlotAtLeastAsWideAsTheWrite() {
    CHECK(tow_ht::ue_reflect::FieldFits(At(0, 8), 8, 64));
    CHECK_MSG(tow_ht::ue_reflect::FieldFits(At(0, 16), 8, 64),
              "a slot wider than the write is fine");
    CHECK_MSG(!tow_ht::ue_reflect::FieldFits(At(0, 4), 8, 64),
              "a slot narrower than the write is not");
    CHECK_MSG(tow_ht::ue_reflect::FieldFits(At(56, 8), 8, 64),
              "a write that ends exactly on the frame end is inside it");
    CHECK_MSG(!tow_ht::ue_reflect::FieldFits(At(60, 8), 8, 64),
              "one that runs a byte past is not");
}

// The offsets come from process memory that may not be a property table at all,
// so the bound is subtracted rather than added: the sum of two values read out
// of noise can wrap past an addition-based check.
void TestFieldFitsSurvivesOffsetsReadOutOfNoise() {
    const std::size_t huge = std::numeric_limits<std::size_t>::max() - 4;
    CHECK_MSG(!tow_ht::ue_reflect::FieldFits(At(huge, 8), 8, 64),
              "an offset near the top of the address space cannot wrap into range");
}

void TestEveryWideEnoughSlotFits() {
    const std::vector<FieldInfo> params = {At(0, 8), At(8, 12), At(20, 12), At(32, 1)};
    const kf::ExpectedWidth widths[] = {
        {"WorldContextObject", 0, 8},
        {"Start",              1, 12},
        {"End",                2, 12},
        {"TraceChannel",       3, 1},
    };
    CHECK(kf::FirstMisfit(params, widths, 4, 64) == kf::kAllFit);
}

// The answer is an index rather than a bool so the caller can name the
// parameter, its reported width and its offset in the line it writes.
void TestTheFirstTooNarrowSlotIsNamed() {
    const std::vector<FieldInfo> params = {At(0, 8), At(8, 4), At(20, 12)};
    const kf::ExpectedWidth widths[] = {
        {"WorldContextObject", 0, 8},
        {"Start",              1, 12},   // the engine calls it 4 bytes wide
        {"End",                2, 12},
    };
    CHECK_MSG(kf::FirstMisfit(params, widths, 3, 64) == 1,
              "the too-narrow Start slot is reported, not the whole frame");
}

void TestAWriteRunningPastTheFrameEndIsRejected() {
    const std::vector<FieldInfo> params = {At(0, 8), At(60, 12)};
    const kf::ExpectedWidth widths[] = {
        {"WorldContextObject", 0, 8},
        {"Start",              1, 12},
    };
    CHECK(kf::FirstMisfit(params, widths, 2, 64) == 1);
}

// Both traces cap PropertiesSize at this before allocating the frame, so a
// garbage size cannot turn into a stack overflow.
void TestTheFrameCapIsBigEnoughForARealTraceFrame() {
    CHECK(kf::kMaxParams >= 256);
    CHECK(sizeof(kf::TArrayHeader) == 16);
    CHECK(alignof(kf::TArrayHeader) == 8);
}

// The other half of the same guard: whatever the frame size, no write the width
// table admits can leave the buffer. A PropertiesSize read out of noise is
// rejected by the cap, and one inside the cap still has to hold every field.
void TestNoAdmittedWriteCanLeaveTheBuffer() {
    const std::vector<FieldInfo> params = {At(kf::kMaxParams - 8, 8)};
    const kf::ExpectedWidth widths[] = {{"WorldContextObject", 0, 8}};
    CHECK_MSG(kf::FirstMisfit(params, widths, 1, kf::kMaxParams) == kf::kAllFit,
              "a write ending exactly on the cap is inside it");
    const std::vector<FieldInfo> past = {At(kf::kMaxParams - 7, 8)};
    CHECK_MSG(kf::FirstMisfit(past, widths, 1, kf::kMaxParams) == 0,
              "one byte past it is not");
    // The frame the traces actually reject: a PropertiesSize wider than the
    // buffer never reaches FirstMisfit, but every offset inside a frame that
    // does must still be bounded by the FRAME, not by the buffer.
    const std::vector<FieldInfo> beyondFrame = {At(120, 8)};
    CHECK_MSG(kf::FirstMisfit(beyondFrame, widths, 1, 64) == 0,
              "an offset past the frame end is rejected even though it fits the cap");
}

// The width table indexes into the resolved-parameter vector, and the two are
// hand-maintained parallel arrays in another file with the correspondence held
// in no type. Dropping or reordering a name on one side without the other used
// to read past the end of the vector, inside the one function whose whole job is
// stopping a write from running off a stack buffer.
void TestAnIndexPastTheResolvedParametersIsAMisfit() {
    std::vector<FieldInfo> params(2);
    params[0].Offset = 0;  params[0].Size = 8;
    params[1].Offset = 8;  params[1].Size = 8;

    const kf::ExpectedWidth widths[] = {
        {"present", 0, 8},
        {"dropped", 5, 8},
    };
    CHECK_MSG(kf::FirstMisfit(params, widths, 2, 64) == kf::kIndexOutOfRange,
              "an index past the resolved parameters answers kIndexOutOfRange, so "
              "the caller cannot feed it back into the vector that came up short");
}

}  // namespace

int main() {
    TestFieldFitsAcceptsASlotAtLeastAsWideAsTheWrite();
    TestFieldFitsSurvivesOffsetsReadOutOfNoise();
    TestEveryWideEnoughSlotFits();
    TestTheFirstTooNarrowSlotIsNamed();
    TestAWriteRunningPastTheFrameEndIsRejected();
    TestTheFrameCapIsBigEnoughForARealTraceFrame();
    TestNoAdmittedWriteCanLeaveTheBuffer();
    TestAnIndexPastTheResolvedParametersIsAMisfit();
    return tow_test::Report();
}

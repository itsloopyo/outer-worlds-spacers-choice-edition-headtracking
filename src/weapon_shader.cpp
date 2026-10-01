// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "weapon_shader.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <limits>

#include "cameraunlock/graphics/dxbc.h"

namespace tow_ht::weapon_shader {
namespace {
using namespace cameraunlock::graphics;
constexpr unsigned kAbsent = std::numeric_limits<unsigned>::max();
constexpr std::array<unsigned, 12> kViewRows{
    16, 17, 18, 20, 21, 22, 99, 100, 101, 103, 104, 105};

unsigned PrimitiveResource(const DxbcContainer& shader,
                           const std::vector<DxbcInstruction>& instructions,
                           unsigned input) {
    const auto& code = shader.code;
    for (std::size_t i = 0; i + 1 < instructions.size(); ++i) {
        const auto& multiply = instructions[i];
        if (multiply.opcode != 35 && multiply.opcode != 38) continue;
        const auto& operands = multiply.operands;
        if (operands.size() != 4) continue;
        const auto& destination = operands[multiply.opcode == 38 ? 1 : 0];
        const auto& source = operands[multiply.opcode == 38 ? 2 : 1];
        const auto& factor = operands[multiply.opcode == 38 ? 3 : 2];
        if (destination.type != 0 || !destination.direct || destination.dimensions != 1 ||
            source.type != 1 || !source.direct || source.dimensions != 1 ||
            source.indices[0] != input || factor.type != 4) continue;
        const auto components = (code[factor.begin] & 3) == 1 ? 1u : 4u;
        bool stride = true;
        const auto mask = (code[destination.begin] >> 4) & 15;
        for (unsigned j = 0; j < 4; ++j)
            if ((mask & (1u << j)) && code[factor.indicesBegin + (components == 1 ? 0 : j)] != 37)
                stride = false;
        if (!stride) continue;
        const auto& load = instructions[i + 1];
        if (load.opcode != 167 || load.operands.size() != 4) continue;
        const auto& address = load.operands[1];
        const auto& resource = load.operands[3];
        if (address.type != 0 || !address.direct || address.dimensions != 1 ||
            address.indices[0] != destination.indices[0] || resource.type != 7 ||
            !resource.direct || resource.dimensions != 1) continue;
        for (const auto& declaration : instructions) {
            const auto at = declaration.begin;
            if (declaration.opcode == 162 && declaration.end - at == 4 &&
                code[at + 2] == resource.indices[0] && code[at + 3] == 16)
                return resource.indices[0];
        }
    }
    return kAbsent;
}
}

std::vector<std::uint32_t> Rewrite(const void* bytes, std::size_t size) {
    const auto shader = ReadDxbc(bytes, size);
    if (shader.code.empty() || shader.code[0] != 0x00010050) return {};
    const auto& code = shader.code;
    const auto instructions = ReadDxbcInstructions(code);
    bool view = false, primitive = false;
    unsigned temps = 0, primitiveInput = kAbsent;
    std::array<bool, 6> rotationReads{};
    for (const auto& instruction : instructions) {
        const auto at = instruction.begin;
        if (instruction.opcode == 89 && instruction.end - at == 4 &&
            (code[at + 1] & 0x7ffff000u) == 0x00208000u) {
            view |= code[at + 2] == 0 && code[at + 3] == 147;
            primitive |= code[at + 2] == 1 && code[at + 3] == 37;
        }
        if (instruction.opcode == 104 && instruction.end - at == 2) temps = code[at + 1];
        for (const auto& operand : instruction.operands) {
            if (operand.type != 8 || !operand.direct || operand.dimensions != 2 ||
                operand.indices[0] != 0) continue;
            for (unsigned j = 0; j < rotationReads.size(); ++j)
                rotationReads[j] = rotationReads[j] || operand.indices[1] == kViewRows[j];
        }
    }
    if (!view || !std::all_of(rotationReads.begin(), rotationReads.end(), [](bool b) { return b; }))
        return {};
    for (const auto& input : shader.inputs)
        if (input.semantic == "ATTRIBUTE" && input.semanticIndex == 13 && input.componentType == 1)
            primitiveInput = input.registerIndex;
    const auto resource = primitiveInput == kAbsent ? kAbsent :
        PrimitiveResource(shader, instructions, primitiveInput);
    const bool gpuScene = resource != kAbsent;
    if (!gpuScene && !primitive) return {};

    std::vector<std::uint32_t> prefix;
    auto emit = [&](std::initializer_list<std::uint32_t> words) {
        prefix.insert(prefix.end(), words);
    };
    auto load = [&](unsigned temporary, unsigned row) {
        if (gpuScene) {
            emit({0x09000023, 0x00100012, temps + 12, 0x0010100a, primitiveInput,
                  0x00004001, 37, 0x00004001, row});
            emit({0x8b0000a7, 0x80008302, 0x00199983, 0x001000f2, temporary,
                  0x0010000a, temps + 12, 0x00004001, 0, 0x00107e46, resource});
        } else {
            emit({0x06000036, 0x001000f2, temporary, 0x00208e46, 1, row});
        }
    };
    load(temps + 13, 35);
    emit({0x07000020, 0x00100082, temps + 13, 0x0010003a, temps + 13,
          0x00004001, kPrimitiveMarker});
    for (unsigned i = 0; i < 9; ++i) {
        load(temps + i, 27 + i);
        emit({0x0a000037, 0x00100072, temps + i, 0x00100ff6, temps + 13,
              0x00100e46, temps + i, 0x00208e46, 0, kViewRows[i]});
        emit({0x06000036, 0x00100082, temps + i, 0x0020803a, 0, kViewRows[i]});
    }
    // The previous inverse rotation is the transpose; nine custom float4s fit
    // both frames only when that redundant matrix is reconstructed here.
    for (unsigned i = 9; i < 12; ++i) {
        for (unsigned j = 0; j < 3; ++j)
            emit({0x05000036, 0x00100002 | (1u << (4 + j)), temps + i,
                  0x0010000a | ((i - 9) << 4), temps + 6 + j});
        emit({0x06000036, 0x00100082, temps + i, 0x0020803a, 0, kViewRows[i]});
        emit({0x0a000037, 0x001000f2, temps + i, 0x00100ff6, temps + 13,
              0x00100e46, temps + i, 0x00208e46, 0, kViewRows[i]});
    }
    std::vector<DxbcConstantRedirect> redirects;
    for (unsigned i = 0; i < kViewRows.size(); ++i) redirects.push_back({0, kViewRows[i], temps + i});
    return WriteDxbc(shader, RedirectDxbcConstants(code, redirects, prefix, 14));
}

} // namespace tow_ht::weapon_shader

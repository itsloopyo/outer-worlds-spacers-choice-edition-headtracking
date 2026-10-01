// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tow_ht::weapon_shader {
constexpr std::uint32_t kPrimitiveMarker = 0x43554854;
std::vector<std::uint32_t> Rewrite(const void* bytes, std::size_t size);
}

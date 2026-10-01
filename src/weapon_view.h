// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include <cstdint>
#include "cameraunlock/unreal/ue_math.h"

namespace tow_ht::weapon_view {
bool Install();
bool Update(std::uintptr_t controller, const cameraunlock::unreal::FRotator& clean);
}

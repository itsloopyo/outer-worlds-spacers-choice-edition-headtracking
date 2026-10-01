// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "build_profile.h"

#include <string>

namespace tow_ht::builds {

struct ImageView {
    const std::uint8_t* data;
    std::size_t size;
    std::uintptr_t base;
};

bool DiscoverOffsets(ImageView image, OffsetTable& offsets, std::string& reason);
bool DiscoverSceneView(ImageView image, std::uint32_t viewPointCaller,
                       std::uint32_t& sceneView, std::string& reason);

}

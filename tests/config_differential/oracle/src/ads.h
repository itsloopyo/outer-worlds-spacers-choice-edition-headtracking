// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "cameraunlock/ads/ads_blend.h"
#include "cameraunlock/ads/ads_fade.h"
#include "cameraunlock/ads/ads_mode.h"
#include "cameraunlock/ads/entry_pose.h"

namespace tow_ht {

using cameraunlock::ads::AdsEntryPose;
using cameraunlock::ads::AdsFade;
using cameraunlock::ads::AdsMode;
using cameraunlock::ads::AdsModeToast;
using cameraunlock::ads::AdsModeValue;
using cameraunlock::ads::AdsSuspendsTracking;
using cameraunlock::ads::BlendAdsPose;
using cameraunlock::ads::kDefaultAdsMode;
using cameraunlock::ads::NextAdsModeTwoSlot;

inline AdsMode ParseAdsMode(const char* text) {
    return cameraunlock::ads::ParseAdsMode(text, false);
}

// The mode in force right now. Written from the hotkey thread and read once per
// frame on the game thread, so it is an atomic rather than a plain member: a
// mode cycled mid-aim has to land on the aim already in progress, which means
// the frame walk reads it live rather than caching it at startup.
AdsMode GetAdsMode();
void SetAdsMode(AdsMode mode);

}  // namespace tow_ht

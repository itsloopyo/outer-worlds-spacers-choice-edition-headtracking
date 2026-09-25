// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace tow_ht {

// The crosshair widgets, as read off a running game by the widget probe.
//
// The tree, from the probe: HUD_BP_C / Reticle (Reticle_BP_C) / RootCanvas
// holds six children, and every one of them is drawn centred on the aim point,
// so all six move together:
//   Crosshair          GenericCrosshair_BP_C - the cross itself
//   ReticuleInteract   the look-at highlight
//   CauseDamageWidget  the hit marker
//   StealthOverlay     the pair of brackets either side of the cross
//   TTDOverlay         the Tactical Time Dilation ring
//   TTDDTOverlay       the Tactical Time Dilation readout
// Leaving one out leaves it parked at screen centre while the cross moves off
// to the shot, which is how StealthOverlay and TTDOverlay were found missing.
//
// Reticle itself is NOT in the list, and that is the finding: translating it
// moves nothing on screen, because its own Blueprint writes RenderTransform
// every tick and overwrites the push. Its children are left alone by that, and
// moving them works - measured, a commanded 177.1 px landed as 177 px.
//
// The outer runs from Reticle up to the game instance. The game instance is
// needed because the live widget tree and the Blueprint template it was built
// from share every link up to HUD_BP_C; only above that do they part company.
// Reticle is needed because CharacterOverview, the health bar, has a TTDOverlay
// of its own under the same game instance.
inline constexpr const char* kCrosshairWidgets =
    "Crosshair@Reticle/WidgetTree/HUD_BP_C/IndianaGameInstance,"
    "ReticuleInteract@Reticle/WidgetTree/HUD_BP_C/IndianaGameInstance,"
    "CauseDamageWidget@Reticle/WidgetTree/HUD_BP_C/IndianaGameInstance,"
    "StealthOverlay@Reticle/WidgetTree/HUD_BP_C/IndianaGameInstance,"
    "TTDOverlay@Reticle/WidgetTree/HUD_BP_C/IndianaGameInstance,"
    "TTDDTOverlay@Reticle/WidgetTree/HUD_BP_C/IndianaGameInstance";

}  // namespace tow_ht

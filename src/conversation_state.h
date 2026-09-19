// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

// Whether the player is in a conversation.
//
// The cursor flag game_state reads goes up for dialogue exactly as it does for
// the pause menu, the inventory and the character sheet, so it cannot tell them
// apart. What does is the dialogue UI itself: ConversationWidget_BP_C holds a
// UUserWidget::InputComponent while it is taking the player's responses and
// drops it when the conversation ends. Measured on build steam-win64-20260804
// against the character screen, the inventory and the pause menu, none of which
// set it; pausing from inside a dialogue leaves it set.
namespace tow_ht::conversation_state {

// Whether a conversation is live right now. Game thread only. Walks the object
// table to find the widget, at most every kWalkIntervalMs and only while no live
// widget answering yes is held, so a dialogue costs one guarded pointer read per
// call once it has been found.
bool Active();

}  // namespace tow_ht::conversation_state

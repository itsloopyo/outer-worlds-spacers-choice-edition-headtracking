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

// Whether a conversation is live right now. Game thread only. While no live
// widget answering yes is held, a call may search one slice of the object table
// for it, so the search is spread over many calls rather than paid in one. Once
// the widget has been found a call is a liveness test and one pointer read.
bool Active();

}  // namespace tow_ht::conversation_state

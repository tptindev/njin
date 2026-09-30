#pragma once

#include "types.h"

namespace sandtable {

// What the player sees over the table (CONCEPT.md, "UI"): as little as can do.
//
// - Top left, a pill with the gang's money, men and turf.
// - Top centre, the day and hour, and pause / play / fast.
// - Bottom centre, a dock of icon buttons, each opening a popup: the men
//   (Đàn em), the turf (Địa bàn), the books (Sổ sách) and, last and dimmer,
//   the generator's and renderer's workings (Gỡ lỗi).
// - A building picked on the map: a small card on the right with what it is,
//   the floors, and whom to send to it.
// - What happens (a shop gives in, money comes home, the day's wages): toasts
//   top right.
//
// A popup is modal: while one is open the clock stops (state.popup_open).

void hud_init(context &ctx, font_handle font);
void hud_cleanup(context &ctx);
// In phase_post_render, after the map's own labels.
void hud_draw(context &ctx);

} // namespace sandtable

#pragma once

#include "types.h"

namespace sandtable {

// The pixel art in assets/sprites.png, made by tools/make_sprites.py. The
// layout numbers here are the ones in that script.

enum class soldier_frame : i32 { idle = 0, walk1, walk2, attack1, attack2, dead };

enum class sprite_variant : i32 { player = 0, enemy, flash };

inline constexpr f32 soldier_cell = 8.0f;
void sprites_init(context &ctx);
void sprites_cleanup(context &ctx);
texture_handle sprite_sheet();

// A soldier with its feet at `feet`, `scale` sheet pixels per world unit
// (1 or 2, whole numbers keep it crisp), mirrored when it faces left.
void draw_soldier_sprite(context &ctx, vec2 feet, arm a, sprite_variant v, soldier_frame f, bool face_left,
                         f32 scale, rgba tint = {1.0f, 1.0f, 1.0f, 1.0f});

// The 5x5 symbol of an arm, its top left at `pos`, `scale` screen pixels per
// symbol pixel, in `tint`.
void draw_arm_symbol(context &ctx, vec2 pos, arm a, f32 scale, rgba tint);

} // namespace sandtable

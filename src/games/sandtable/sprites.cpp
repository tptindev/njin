#include "sprites.h"

namespace sandtable {

namespace {

// The arm symbols sit under the old chip art in the sheet.
constexpr f32 symbol_y = 168.0f + 25.0f * static_cast<f32>(arm_count);

texture_handle sheet{};

} // namespace

void sprites_init(context &ctx) {
  sheet = texture_load(ctx, "assets/sprites.png");
  if (sheet.id != 0)
    texture_set_filter(ctx, sheet, filter_nearest);
}

void sprites_cleanup(context &ctx) {
  if (sheet.id != 0) {
    texture_unload(ctx, sheet);
    sheet = {};
  }
}

texture_handle sprite_sheet() { return sheet; }

void draw_soldier_sprite(context &ctx, vec2 feet, arm a, sprite_variant v, soldier_frame f, bool face_left,
                         f32 scale, rgba tint) {
  const f32 row = static_cast<f32>(static_cast<i32>(a) * 3 + static_cast<i32>(v));
  const rect src{{static_cast<f32>(f) * soldier_cell, row * soldier_cell}, {soldier_cell, soldier_cell}};
  texture_draw_ex(ctx, sheet, texture_draw_desc{.pos = feet,
                                                .source = src,
                                                .scale = {scale, scale},
                                                .origin = {0.5f, 1.0f},
                                                .flip_x = face_left,
                                                .tint = tint});
}

void draw_arm_symbol(context &ctx, vec2 pos, arm a, f32 scale, rgba tint) {
  const rect src{{static_cast<f32>(a) * 6.0f, symbol_y}, {5.0f, 5.0f}};
  texture_draw_ex(ctx, sheet, texture_draw_desc{.pos = pos, .source = src, .scale = {scale, scale}, .tint = tint});
}

} // namespace sandtable

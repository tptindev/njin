#pragma once
#include "_mod.h"
#include "_types.h"
#include <entt/entity/entity.hpp>
#include <raylib.h>
#include <unordered_map>
#include <unordered_set>

namespace njin {
// Core module. Advances sprite_anim and bakes dirty tilemap chunks in
// phase_post_update, then draws every sprite, tilemap and particle emitter
// in phase_render, ordered by layer.
mod_desc sprite_module();

// One tilemap chunk drawn once into its own image. Redrawn only when the
// chunk's version moves on, so a static map costs one textured quad per
// visible chunk per frame.
struct chunk_image {
  RenderTexture2D target{};
  u32 version = 0;
  u32 texture_version = 0; // tileset texture_slot::version it was drawn from
  u32 last_used = 0; // frame number it was last visible on
  // Animated tiles are left out of the image and drawn over it every frame.
  bool has_animated = false;
};

// Chunk images per tilemap entity, keyed by tile_chunk_key. Images of chunks
// that stay off screen for a while are freed, and so are those of chunks or
// tilemaps that no longer exist.
struct sprite_cache {
  std::unordered_map<entt::entity, std::unordered_map<u64, chunk_image>> chunks;
  u32 frame = 0;
  // Game time for animated tiles: stops with the game, follows its speed.
  f32 tile_time = 0.0f;
  // Draw layers sorted by y (draw_set_y_sort).
  std::unordered_set<i32> y_sorted;

  sprite_cache() = default;
  ~sprite_cache();
  sprite_cache(const sprite_cache &) = delete;
  sprite_cache &operator=(const sprite_cache &) = delete;
};
} // namespace njin

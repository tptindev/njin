#pragma once
#include "_mod.h"
#include "_types.h"
#include <entt/entity/entity.hpp>
#include <raylib.h>
#include <unordered_map>

namespace njin {
// Core module. Advances sprite_anim and bakes dirty tilemap chunks in
// phase_post_update, then draws every sprite and tilemap in phase_render,
// ordered by layer.
mod_desc sprite_module();

// One tilemap chunk drawn once into its own image. Redrawn only when the
// chunk's version moves on, so a static map costs one textured quad per
// visible chunk per frame.
struct chunk_image {
  RenderTexture2D target{};
  u32 version = 0;
  u32 last_used = 0; // frame number it was last visible on
};

// Chunk images per tilemap entity, keyed by tile_chunk_key. Images of chunks
// that stay off screen for a while are freed, and so are those of chunks or
// tilemaps that no longer exist.
struct sprite_cache {
  std::unordered_map<entt::entity, std::unordered_map<u64, chunk_image>> chunks;
  u32 frame = 0;

  sprite_cache() = default;
  ~sprite_cache();
  sprite_cache(const sprite_cache &) = delete;
  sprite_cache &operator=(const sprite_cache &) = delete;
};
} // namespace njin

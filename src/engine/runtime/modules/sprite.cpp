#include "sprite.h"
#include "_comps.h"
#include "_tilemap.h"
#include "njin2rl.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "fx.h"
#include "particles.h"
#include <algorithm>
#include <vector>

namespace njin {
namespace {
// A chunk image off screen for this many frames is freed. Long enough that
// panning back and forth does not rebake, short enough that a large map
// explored once does not keep every chunk it ever showed.
constexpr u32 chunk_evict_frames = 300;

void release(chunk_image &image) {
  if (IsRenderTextureValid(image.target))
    UnloadRenderTexture(image.target);
  image = chunk_image{};
}

vec2 chunk_pixel_size(const tilemap &map) {
  return map.tile_size * (f32)tile_chunk_size;
}

// World-space rectangle covered by the chunk at `key`.
rect chunk_rect(const tilemap &map, vec2 origin, u64 key) {
  const cell c = tile_chunk_coord(key);
  const vec2 size = chunk_pixel_size(map);
  return {{origin.x + (f32)c.x * size.x, origin.y + (f32)c.y * size.y}, size};
}

// Draws every tile of `chunk` with its top-left corner at `offset`. Used both
// to bake a chunk image and, when a chunk changed after baking this frame, to
// draw it directly so a stale image never reaches the screen.
void draw_chunk_tiles(const texture_slot &tileset, const tilemap &map,
                      const tile_chunk &chunk, vec2 offset, Color tint) {
  const i32 columns = (i32)((f32)tileset.texture.width / map.tile_size.x);
  if (columns <= 0)
    return;
  for (i32 y = 0; y < tile_chunk_size; y++) {
    for (i32 x = 0; x < tile_chunk_size; x++) {
      const i32 id = chunk.tiles[(usize)(y * tile_chunk_size + x)];
      if (id < 0)
        continue;
      const Rectangle source{(f32)(id % columns) * map.tile_size.x,
                             (f32)(id / columns) * map.tile_size.y,
                             map.tile_size.x, map.tile_size.y};
      const Vector2 pos{offset.x + (f32)x * map.tile_size.x,
                        offset.y + (f32)y * map.tile_size.y};
      DrawTextureRec(tileset.texture, source, pos, tint);
    }
  }
}

// Brings the image of one chunk up to date. Runs outside the frame's drawing
// (in phase_post_update): BeginTextureMode resets the camera transform, so it
// must not run while the world pass is open.
void bake(chunk_image &image, const texture_slot &tileset, const tilemap &map,
          const tile_chunk &chunk) {
  const vec2 size = chunk_pixel_size(map);
  const i32 w = (i32)size.x;
  const i32 h = (i32)size.y;
  if (!IsRenderTextureValid(image.target) || image.target.texture.width != w ||
      image.target.texture.height != h) {
    release(image);
    image.target = LoadRenderTexture(w, h);
    if (!IsRenderTextureValid(image.target))
      return;
  }
  BeginTextureMode(image.target);
  ClearBackground(BLANK);
  draw_chunk_tiles(tileset, map, chunk, {0.0f, 0.0f}, WHITE);
  EndTextureMode();
  SetTextureFilter(image.target.texture, texture_filter_to_raylib(tileset.filter));
  image.version = chunk.version;
}

void animate(njin_ctx &ctx) {
  const f32 dt = ctx.time.dt;
  auto view = ctx.ecs.registry.view<sprite, sprite_anim>();
  for (auto [entity, spr, anim] : view.each()) {
    if (anim.frame_size.x <= 0.0f || anim.frame_size.y <= 0.0f ||
        anim.count <= 0)
      continue;
    if (anim.playing && !anim.finished && anim.fps > 0.0f) {
      anim.time += dt;
      i32 frame = (i32)(anim.time * anim.fps);
      if (frame >= anim.count) {
        if (anim.loop) {
          frame %= anim.count;
          // Keep time bounded so a long-running loop does not lose precision.
          anim.time = std::fmod(anim.time, (f32)anim.count / anim.fps);
        } else {
          frame = anim.count - 1;
          anim.finished = true;
        }
      }
      anim.frame = frame;
    }
    const texture_slot *slot = texture_slot_of(ctx.texture, spr.texture);
    if (slot == nullptr)
      continue;
    const i32 columns = (i32)((f32)slot->texture.width / anim.frame_size.x);
    if (columns <= 0)
      continue;
    const i32 index = anim.first + anim.frame;
    spr.source = rect{{(f32)(index % columns) * anim.frame_size.x,
                       (f32)(index / columns) * anim.frame_size.y},
                      anim.frame_size};
  }
}

void bake_tilemaps(njin_ctx &ctx) {
  sprite_cache &cache = ctx.sprites;
  const u32 frame = ++cache.frame;
  const rect view = camera_bounds(ctx);
  entt::registry &registry = ctx.ecs.registry;

  for (auto [entity, tr, map] : registry.view<transform, tilemap>().each()) {
    const texture_slot *tileset = texture_slot_of(ctx.texture, map.tileset);
    if (!map.visible || tileset == nullptr)
      continue;
    auto &images = cache.chunks[entity];
    for (const auto &[key, chunk] : map.chunks) {
      if (!rects_overlap(chunk_rect(map, tr.pos, key), view))
        continue;
      chunk_image &image = images[key];
      image.last_used = frame;
      if (image.version != chunk.version || !IsRenderTextureValid(image.target))
        bake(image, *tileset, map, chunk);
    }
  }

  // Evict: gone tilemaps, gone chunks, and chunks unseen for a while.
  for (auto it = cache.chunks.begin(); it != cache.chunks.end();) {
    const tilemap *map = registry.valid(it->first)
                             ? registry.try_get<tilemap>(it->first)
                             : nullptr;
    auto &images = it->second;
    for (auto img = images.begin(); img != images.end();) {
      const bool gone = map == nullptr || !map->chunks.contains(img->first);
      if (gone || frame - img->second.last_used > chunk_evict_frames) {
        release(img->second);
        img = images.erase(img);
      } else {
        ++img;
      }
    }
    it = images.empty() ? cache.chunks.erase(it) : std::next(it);
  }
}

void draw_tilemap(njin_ctx &ctx, entt::entity entity, const transform &tr,
                  const tilemap &map, const rect &view) {
  const texture_slot *tileset = texture_slot_of(ctx.texture, map.tileset);
  if (tileset == nullptr)
    return;
  Color tint{};
  to_raylib(map.tint, tint);
  const auto cached = ctx.sprites.chunks.find(entity);
  for (const auto &[key, chunk] : map.chunks) {
    const rect area = chunk_rect(map, tr.pos, key);
    if (!rects_overlap(area, view))
      continue;
    const chunk_image *image = nullptr;
    if (cached != ctx.sprites.chunks.end()) {
      const auto it = cached->second.find(key);
      if (it != cached->second.end() && it->second.version == chunk.version &&
          IsRenderTextureValid(it->second.target))
        image = &it->second;
    }
    if (image == nullptr) {
      // Changed after this frame's bake, or not baked yet: draw it tile by
      // tile this once. The bake catches up next frame.
      draw_chunk_tiles(*tileset, map, chunk, area.pos, tint);
      continue;
    }
    const Texture2D &texture = image->target.texture;
    const Rectangle source{0.0f, 0.0f, (f32)texture.width, -(f32)texture.height};
    const Rectangle dest{area.pos.x, area.pos.y, area.size.x, area.size.y};
    DrawTexturePro(texture, source, dest, Vector2{0.0f, 0.0f}, 0.0f, tint);
  }
}

// One thing to draw this frame. On the same layer tilemaps come first, so a
// map is the ground under the sprites standing on it, and particles last, so
// dust and sparks show over the sprite that made them.
struct draw_item {
  i32 layer = 0;
  i32 kind = 0; // 0 tilemap, 1 sprite, 2 particle emitter
  entt::entity entity{};
};

void draw(njin_ctx &ctx) {
  entt::registry &registry = ctx.ecs.registry;
  std::vector<draw_item> items;
  for (auto [entity, tr, map] : registry.view<const transform, const tilemap>().each()) {
    if (map.visible)
      items.push_back({map.layer, 0, entity});
  }
  for (auto [entity, tr, spr] : registry.view<const transform, const sprite>().each()) {
    if (spr.visible)
      items.push_back({spr.layer, 1, entity});
  }
  for (auto [entity, tr, em] :
       registry.view<const transform, const particle_emitter>().each()) {
    if (em.visible && !em.particles.empty())
      items.push_back({em.layer, 2, entity});
  }
  std::stable_sort(items.begin(), items.end(),
                   [](const draw_item &a, const draw_item &b) {
                     return a.layer != b.layer ? a.layer < b.layer
                                               : a.kind < b.kind;
                   });

  const rect view = camera_bounds(ctx);
  for (const draw_item &item : items) {
    const transform &tr = registry.get<transform>(item.entity);
    if (item.kind == 0) {
      draw_tilemap(ctx, item.entity, tr, registry.get<tilemap>(item.entity), view);
      continue;
    }
    if (item.kind == 2) {
      particles_draw(ctx, tr, registry.get<particle_emitter>(item.entity));
      continue;
    }
    const sprite &spr = registry.get<sprite>(item.entity);
    const flash_fx *flash = registry.try_get<flash_fx>(item.entity);
    const bool flashing = flash != nullptr && fx_flash_begin(ctx, *flash);
    texture_store_draw_ex(ctx.texture, spr.texture,
                          texture_draw_desc{.pos = tr.pos,
                                            .source = spr.source,
                                            .scale = {tr.scale, tr.scale},
                                            .origin = spr.origin,
                                            .rotation = tr.rot,
                                            .flip_x = spr.flip_x,
                                            .flip_y = spr.flip_y,
                                            .tint = spr.tint});
    if (flashing)
      fx_flash_end();
  }
}

void setup(njin_ctx &ctx) {
  ecs_register(ctx, phase_post_update, animate);
  ecs_register(ctx, phase_post_update, fx_update_sprite_flashes);
  ecs_register(ctx, phase_post_update, bake_tilemaps);
  ecs_register(ctx, phase_render, draw);
}
} // namespace

sprite_cache::~sprite_cache() {
  for (auto &[entity, images] : chunks) {
    for (auto &[key, image] : images)
      release(image);
  }
}

mod_desc sprite_module() {
  return mod_desc{.name = "njin.sprite", .setup = setup};
}
} // namespace njin

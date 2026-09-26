#include "sprite.h"
#include "_comps.h"
#include "_tilemap.h"
#include "njin2rl.h"
#include "njin_ctx.h"
#include "njin_camera.h"
#include "njin_ctx_impl.h"
#include "fx.h"
#include "particles.h"
#include "_collide.h"
#include <algorithm>
#include <cmath>
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

// Which tiles draw_chunk_tiles draws.
enum class tiles_pass { all, still };

bool is_animated(const tilemap &map, i32 value) {
  return !map.anims.empty() && map.anims.contains(tile_id(value));
}

// Draws the tiles of `chunk` with its top-left corner at `offset`. Used to
// bake a chunk image (still tiles only) and, when a chunk changed after baking
// this frame, to draw it all directly so a stale image never reaches the
// screen. The animated tiles over a baked image go through draw_animated_tiles.
void draw_chunk_tiles(const texture_slot &tileset, const tilemap &map,
                      const tile_chunk &chunk, vec2 offset, Color tint,
                      tiles_pass pass = tiles_pass::all, f32 time = 0.0f) {
  const vec2 step = map.tile_size + vec2{map.spacing, map.spacing};
  const Rectangle area = texture_area(tileset);
  const i32 columns = (i32)((area.width - 2.0f * map.margin + map.spacing) / step.x);
  if (columns <= 0)
    return;
  for (i32 y = 0; y < tile_chunk_size; y++) {
    for (i32 x = 0; x < tile_chunk_size; x++) {
      const i32 value = chunk.tiles[(usize)(y * tile_chunk_size + x)];
      if (value < 0)
        continue;
      i32 id = tile_id(value);
      if (!map.anims.empty()) {
        const auto anim = map.anims.find(id);
        const bool animated = anim != map.anims.end();
        if (pass == tiles_pass::still && animated)
          continue;
        if (animated) {
          const i32 frame = tile_anim_frame(anim->second, time);
          if (frame >= 0)
            id = frame;
        }
      }
      // A negative source size flips the tile.
      const Rectangle source{area.x + map.margin + (f32)(id % columns) * step.x,
                             area.y + map.margin + (f32)(id / columns) * step.y,
                             (value & tile_flip_x) != 0 ? -map.tile_size.x : map.tile_size.x,
                             (value & tile_flip_y) != 0 ? -map.tile_size.y : map.tile_size.y};
      const Vector2 pos{offset.x + (f32)x * map.tile_size.x,
                        offset.y + (f32)y * map.tile_size.y};
      DrawTextureRec(tileset.texture, source, pos, tint);
    }
  }
}

// Draws the animated tiles of `chunk` listed in `cells`, at their frame for
// `time`, over its baked image.
void draw_animated_tiles(const texture_slot &tileset, const tilemap &map,
                         const tile_chunk &chunk, const std::vector<u16> &cells,
                         vec2 offset, Color tint, f32 time) {
  const vec2 step = map.tile_size + vec2{map.spacing, map.spacing};
  const Rectangle area = texture_area(tileset);
  const i32 columns = (i32)((area.width - 2.0f * map.margin + map.spacing) / step.x);
  if (columns <= 0)
    return;
  for (const u16 cell : cells) {
    const i32 value = chunk.tiles[cell];
    i32 id = tile_id(value);
    const auto anim = map.anims.find(id);
    if (anim != map.anims.end()) {
      const i32 frame = tile_anim_frame(anim->second, time);
      if (frame >= 0)
        id = frame;
    }
    const Rectangle source{area.x + map.margin + (f32)(id % columns) * step.x,
                           area.y + map.margin + (f32)(id / columns) * step.y,
                           (value & tile_flip_x) != 0 ? -map.tile_size.x : map.tile_size.x,
                           (value & tile_flip_y) != 0 ? -map.tile_size.y : map.tile_size.y};
    const Vector2 pos{offset.x + (f32)(cell % tile_chunk_size) * map.tile_size.x,
                      offset.y + (f32)(cell / tile_chunk_size) * map.tile_size.y};
    DrawTextureRec(tileset.texture, source, pos, tint);
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
  draw_chunk_tiles(tileset, map, chunk, {0.0f, 0.0f}, WHITE, tiles_pass::still);
  EndTextureMode();
  image.animated_cells.clear();
  if (!map.anims.empty()) {
    for (usize cell = 0; cell < chunk.tiles.size(); cell++) {
      if (chunk.tiles[cell] >= 0 && is_animated(map, chunk.tiles[cell]))
        image.animated_cells.push_back((u16)cell);
    }
  }
  SetTextureFilter(image.target.texture, texture_filter_to_raylib(tileset.filter));
  image.version = chunk.version;
  image.texture_version = tileset.version;
}

void animate(njin_ctx &ctx) {
  const f32 dt = ctx.time.dt;
  ctx.sprites.tile_time += dt;
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
    const i32 columns = (i32)(texture_area(*slot).width / anim.frame_size.x);
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
  ctx.stats.reset();
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
      if (image.version != chunk.version || image.texture_version != tileset->version ||
          !IsRenderTextureValid(image.target))
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
  // Chunks with animated tiles: those are drawn after every chunk image, all
  // from the tileset, so the tileset texture is one batch instead of one per chunk.
  struct animated_chunk {
    const tile_chunk *chunk;
    const chunk_image *image;
    vec2 pos;
  };
  std::vector<animated_chunk> animated;
  for (const auto &[key, chunk] : map.chunks) {
    const rect area = chunk_rect(map, tr.pos, key);
    if (!rects_overlap(area, view))
      continue;
    const chunk_image *image = nullptr;
    if (cached != ctx.sprites.chunks.end()) {
      const auto it = cached->second.find(key);
      if (it != cached->second.end() && it->second.version == chunk.version &&
          it->second.texture_version == tileset->version &&
          IsRenderTextureValid(it->second.target))
        image = &it->second;
    }
    if (image == nullptr) {
      // Changed after this frame's bake, or not baked yet: draw it tile by
      // tile this once. The bake catches up next frame.
      ctx.stats.tile_chunks++;
      ctx.stats.note_draw(tileset->texture.id, blend_alpha);
      draw_chunk_tiles(*tileset, map, chunk, area.pos, tint, tiles_pass::all,
                       ctx.sprites.tile_time);
      continue;
    }
    const Texture2D &texture = image->target.texture;
    const Rectangle source{0.0f, 0.0f, (f32)texture.width, -(f32)texture.height};
    const Rectangle dest{area.pos.x, area.pos.y, area.size.x, area.size.y};
    ctx.stats.tile_chunks++;
    ctx.stats.note_draw(texture.id, blend_alpha);
    DrawTexturePro(texture, source, dest, Vector2{0.0f, 0.0f}, 0.0f, tint);
    if (!image->animated_cells.empty())
      animated.push_back({&chunk, image, area.pos});
  }
  if (!animated.empty())
    ctx.stats.note_draw(tileset->texture.id, blend_alpha);
  for (const animated_chunk &a : animated)
    draw_animated_tiles(*tileset, map, *a.chunk, a.image->animated_cells, a.pos, tint,
                        ctx.sprites.tile_time);
}

// False when `spr` is certainly outside `view`. The box is a circle around the
// anchor that reaches the farthest corner, so rotation cannot pull it back in.
bool sprite_on_screen(const njin_ctx &ctx, const transform &tr, const sprite &spr,
                      const rect &view) {
  const texture_slot *slot = texture_slot_of(ctx.texture, spr.texture);
  if (slot == nullptr)
    return false; // nothing would be drawn
  const bool whole = spr.source.size.x == 0.0f || spr.source.size.y == 0.0f;
  const Rectangle area = texture_area(*slot);
  const f32 w = (whole ? area.width : spr.source.size.x) * std::abs(tr.scale);
  const f32 h = (whole ? area.height : spr.source.size.y) * std::abs(tr.scale);
  const f32 radius = std::hypot(std::max(spr.origin.x, 1.0f - spr.origin.x) * w,
                                std::max(spr.origin.y, 1.0f - spr.origin.y) * h);
  return rects_overlap(rect{tr.pos - vec2{radius, radius}, {2.0f * radius, 2.0f * radius}}, view);
}

// One thing to draw this frame. On the same layer tilemaps come first, so a
// map is the ground under the sprites standing on it, and particles last, so
// dust and sparks show over the sprite that made them.
struct draw_item {
  i32 layer = 0;
  i32 kind = 0; // 0 tilemap, 1 sprite, 2 particle emitter
  entt::entity entity{};
  f32 y = 0.0f; // for layers sorted by y
};

void draw(njin_ctx &ctx) {
  entt::registry &registry = ctx.ecs.registry;
  render_stats &stats = ctx.stats;
  const rect view = camera_bounds(ctx);
  // A shaking camera reveals a little of what lies outside `view`.
  const bool cull = ctx.fx.trauma <= 0.0f;
  std::vector<draw_item> items;
  for (auto [entity, tr, map] : registry.view<const transform, const tilemap>().each()) {
    if (map.visible)
      items.push_back({map.layer, 0, entity});
  }
  for (auto [entity, tr, spr] : registry.view<const transform, const sprite>().each()) {
    if (!spr.visible)
      continue;
    if (cull && !sprite_on_screen(ctx, tr, spr, view)) {
      stats.sprites_culled++;
      continue;
    }
    items.push_back({spr.layer, 1, entity, tr.pos.y + spr.sort_offset});
  }
  for (auto [entity, tr, em] :
       registry.view<const transform, const particle_emitter>().each()) {
    if (!em.visible || em.particles.empty())
      continue;
    const particle_extent *extent = registry.try_get<particle_extent>(entity);
    if (cull && extent != nullptr && !particles_on_screen(tr, em, *extent, view)) {
      stats.emitters_culled++;
      continue;
    }
    items.push_back({em.layer, 2, entity, tr.pos.y});
  }
  const std::unordered_set<i32> &by_y = ctx.sprites.y_sorted;
  std::stable_sort(items.begin(), items.end(),
                   [&by_y](const draw_item &a, const draw_item &b) {
                     if (a.layer != b.layer)
                       return a.layer < b.layer;
                     // Tilemaps stay first, as the ground; the rest by y.
                     if (!by_y.empty() && a.kind != 0 && b.kind != 0 && by_y.contains(a.layer) &&
                         a.y != b.y)
                       return a.y < b.y;
                     return a.kind < b.kind;
                   });

  for (const draw_item &item : items) {
    const transform &tr = registry.get<transform>(item.entity);
    if (item.kind == 0) {
      draw_tilemap(ctx, item.entity, tr, registry.get<tilemap>(item.entity), view);
      continue;
    }
    if (item.kind == 2) {
      particles_draw(ctx, item.entity, tr, registry.get<particle_emitter>(item.entity));
      continue;
    }
    const sprite &spr = registry.get<sprite>(item.entity);
    const flash_fx *flash = registry.try_get<flash_fx>(item.entity);
    const dissolve_fx *dissolve = registry.try_get<dissolve_fx>(item.entity);
    if (dissolve != nullptr && fx_dissolve_hidden(*dissolve))
      continue; // dissolved away: nothing of it is left to draw
    // One shader at a time: a dissolving sprite takes the flash into its own pass.
    const bool flashing = dissolve != nullptr ? fx_dissolve_begin(ctx, *dissolve, flash)
                                              : (flash != nullptr && fx_flash_begin(ctx, *flash));
    stats.sprites++;
    if (flashing)
      stats.note_flush();
    if (const texture_slot *slot = texture_slot_of(ctx.texture, spr.texture))
      stats.note_draw(slot->texture.id, blend_alpha);
    texture_store_draw_ex(ctx.texture, spr.texture,
                          texture_draw_desc{.pos = tr.pos,
                                            .source = spr.source,
                                            .scale = {tr.scale, tr.scale},
                                            .origin = spr.origin,
                                            .rotation = tr.rot,
                                            .flip_x = spr.flip_x,
                                            .flip_y = spr.flip_y,
                                            .tint = spr.tint});
    if (flashing) {
      fx_sprite_shader_end();
      stats.note_flush();
    }
  }
}

void setup(njin_ctx &ctx) {
  ecs_register(ctx, phase_post_update, animate, "animate");
  ecs_register(ctx, phase_post_update, fx_update_sprite_flashes, "fx_update_sprite_flashes");
  ecs_register(ctx, phase_post_update, fx_update_sprite_dissolves, "fx_update_sprite_dissolves");
  ecs_register(ctx, phase_post_update, bake_tilemaps, "bake_tilemaps");
  ecs_register(ctx, phase_render, draw, "draw");
}
} // namespace

sprite_cache::~sprite_cache() {
  for (auto &[entity, images] : chunks) {
    for (auto &[key, image] : images)
      release(image);
  }
}

void draw_set_y_sort(njin_ctx &ctx, i32 layer, bool on) {
  if (on)
    ctx.sprites.y_sorted.insert(layer);
  else
    ctx.sprites.y_sorted.erase(layer);
}

mod_desc sprite_module() {
  return mod_desc{.name = "njin.sprite", .setup = setup};
}
} // namespace njin

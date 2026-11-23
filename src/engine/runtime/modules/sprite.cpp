#include "sprite.h"
#include "_comps.h"
#include "_tilemap.h"
#include "njin2rl.h"
#include "njin_ctx.h"
#include "njin_camera.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include "njin_draw_shapes.h"
#include "njin_draw_texture.h"
#include "camera.h"
#include "render_stats.h"
#include "njin_ecs.h"
#include "njin_texture.h"
#include "fx.h"
#include "particles.h"
#include "particles_gpu.h"
#include "_collide.h"
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <vector>
#include <stdexcept>
#include "njin_window.h"
#include "njin_draw_portable.h"

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

void animate(context &ctx) {
  const f32 dt = ctx_ref<time_state>(ctx).dt;
  ctx_ref<sprite_cache>(ctx)->tile_time += dt;
  auto view = ctx_ref<ecs_store>(ctx)->registry.view<sprite, sprite_anim>();
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
    const texture_slot *slot = texture_slot_of(ctx_ref<texture_store>(ctx), spr.texture);
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

void bake_tilemaps(context &ctx) {
  sprite_cache &cache = ctx_ref<sprite_cache>(ctx);
  const u32 frame = ++cache.frame;
  ctx_ref<render_stats>(ctx)->reset();
  const rect view = camera_bounds(ctx);
  entt::registry &registry = ctx_ref<ecs_store>(ctx)->registry;

  for (auto [entity, tr, map] : registry.view<transform, tilemap>().each()) {
    const texture_slot *tileset = texture_slot_of(ctx_ref<texture_store>(ctx), map.tileset);
    // Non-square grids are drawn tile by tile (draw_shaped_tiles), not baked.
    if (!map.visible || tileset == nullptr || map.orientation != tile_orthogonal)
      continue;
    auto &images = cache.chunks[entity];
    for (const auto &[key, chunk] : map.chunks) {
      if (!rects_overlap(chunk_rect(map, tr.pos, key), view))
        continue;
      chunk_image &image = images[key];
      if (image.version != chunk.version || image.texture_version != tileset->version ||
          !IsRenderTextureValid(image.target))
        bake(image, *tileset, map, chunk);
      // After the bake: making a new image resets the whole chunk_image, and a last_used of 0
      // would evict it below once the game has run chunk_evict_frames frames, so it would be
      // baked again every frame.
      image.last_used = frame;
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

// Isometric, staggered and hexagonal maps: every cell whose image can reach
// the view, straight from the tileset, in the order Tiled paints them, so an
// image taller than its cell (a tree, a wall) covers the cells behind it. The
// image sits on the bottom-left corner of the cell's box.
void draw_shaped_tiles(context &ctx, const texture_slot &tileset, vec2 origin, const tilemap &map,
                       const rect &view, Color tint) {
  const vec2 step = map.tile_size + vec2{map.spacing, map.spacing};
  const Rectangle area = texture_area(tileset);
  const i32 columns = (i32)((area.width - 2.0f * map.margin + map.spacing) / step.x);
  if (columns <= 0)
    return;
  const vec2 cell_box = map.cell_size.x > 0.0f && map.cell_size.y > 0.0f ? map.cell_size : map.tile_size;
  // An image reaches above its cell by the extra height, and right by the
  // extra width: cells that far below or left of the view still show.
  const f32 rise = std::max(0.0f, map.tile_size.y - cell_box.y);
  const f32 wide = std::max(0.0f, map.tile_size.x - cell_box.x);
  const rect r{{view.pos.x - wide, view.pos.y}, {view.size.x + wide, view.size.y + rise}};
  cell lo{INT32_MAX, INT32_MAX}, hi{INT32_MIN, INT32_MIN};
  for (const vec2 p : {r.pos, r.pos + vec2{r.size.x, 0.0f}, r.pos + vec2{0.0f, r.size.y}, r.pos + r.size}) {
    const cell c = tilemap_cell_at(map, origin, p);
    lo = {std::min(lo.x, c.x), std::min(lo.y, c.y)};
    hi = {std::max(hi.x, c.x), std::max(hi.y, c.y)};
  }
  lo = {lo.x - 1, lo.y - 1};
  hi = {hi.x + 1, hi.y + 1};
  const f32 time = ctx_ref<sprite_cache>(ctx)->tile_time;
  const auto draw = [&](i32 x, i32 y) {
    const i32 value = tilemap_get(map, x, y);
    if (value < 0)
      return;
    i32 id = tile_id(value);
    if (!map.anims.empty()) {
      const auto anim = map.anims.find(id);
      if (anim != map.anims.end()) {
        const i32 frame = tile_anim_frame(anim->second, time);
        if (frame >= 0)
          id = frame;
      }
    }
    const rect box = tilemap_cell_rect(map, origin, x, y);
    const Rectangle source{area.x + map.margin + (f32)(id % columns) * step.x,
                           area.y + map.margin + (f32)(id / columns) * step.y,
                           (value & tile_flip_x) != 0 ? -map.tile_size.x : map.tile_size.x,
                           (value & tile_flip_y) != 0 ? -map.tile_size.y : map.tile_size.y};
    DrawTextureRec(tileset.texture, source, Vector2{box.pos.x, box.pos.y + box.size.y - map.tile_size.y}, tint);
  };
  if (map.orientation == tile_isometric) {
    // Back to front: by x + y (the diamond's screen row), then left to right.
    for (i32 d = lo.x + lo.y; d <= hi.x + hi.y; d++)
      for (i32 x = std::max(lo.x, d - hi.y); x <= std::min(hi.x, d - lo.y); x++)
        draw(x, d - x);
  } else if (map.stagger_x) {
    // Row by row; in each, the raised columns before the lowered ones.
    const auto shifted = [&](i32 i) { return ((i & 1) != 0) != map.stagger_even; };
    for (i32 y = lo.y; y <= hi.y; y++)
      for (const bool lowered : {false, true})
        for (i32 x = lo.x; x <= hi.x; x++)
          if (shifted(x) == lowered)
            draw(x, y);
  } else {
    for (i32 y = lo.y; y <= hi.y; y++)
      for (i32 x = lo.x; x <= hi.x; x++)
        draw(x, y);
  }
}

void draw_tilemap(context &ctx, entt::entity entity, const transform &tr,
                  const tilemap &map, const rect &view) {
  const texture_slot *tileset = texture_slot_of(ctx_ref<texture_store>(ctx), map.tileset);
  if (tileset == nullptr)
    return;
  Color tint{};
  to_raylib(map.tint, tint);
  if (map.orientation != tile_orthogonal) {
    ctx_ref<render_stats>(ctx)->note_draw(tileset->texture.id, blend_alpha);
    draw_shaped_tiles(ctx, *tileset, tr.pos, map, view, tint);
    return;
  }
  const auto cached = ctx_ref<sprite_cache>(ctx)->chunks.find(entity);
  // Chunks with animated tiles: those are drawn after every chunk image, all
  // from the tileset, so the tileset texture is one batch instead of one per chunk.
  struct animated_chunk {
    const tile_chunk *chunk;
    const chunk_image *image;
    vec2 pos;
  };
  // Kept between frames: no allocation once it has grown.
  thread_local std::vector<animated_chunk> animated;
  animated.clear();
  for (const auto &[key, chunk] : map.chunks) {
    const rect area = chunk_rect(map, tr.pos, key);
    if (!rects_overlap(area, view))
      continue;
    const chunk_image *image = nullptr;
    if (cached != ctx_ref<sprite_cache>(ctx)->chunks.end()) {
      const auto it = cached->second.find(key);
      if (it != cached->second.end() && it->second.version == chunk.version &&
          it->second.texture_version == tileset->version &&
          IsRenderTextureValid(it->second.target))
        image = &it->second;
    }
    if (image == nullptr) {
      // Changed after this frame's bake, or not baked yet: draw it tile by
      // tile this once. The bake catches up next frame.
      ctx_ref<render_stats>(ctx)->tile_chunks++;
      ctx_ref<render_stats>(ctx)->note_draw(tileset->texture.id, blend_alpha);
      draw_chunk_tiles(*tileset, map, chunk, area.pos, tint, tiles_pass::all,
                       ctx_ref<sprite_cache>(ctx)->tile_time);
      continue;
    }
    const Texture2D &texture = image->target.texture;
    const Rectangle source{0.0f, 0.0f, (f32)texture.width, -(f32)texture.height};
    const Rectangle dest{area.pos.x, area.pos.y, area.size.x, area.size.y};
    ctx_ref<render_stats>(ctx)->tile_chunks++;
    ctx_ref<render_stats>(ctx)->note_draw(texture.id, blend_alpha);
    DrawTexturePro(texture, source, dest, Vector2{0.0f, 0.0f}, 0.0f, tint);
    if (!image->animated_cells.empty())
      animated.push_back({&chunk, image, area.pos});
  }
  if (!animated.empty())
    ctx_ref<render_stats>(ctx)->note_draw(tileset->texture.id, blend_alpha);
  for (const animated_chunk &a : animated)
    draw_animated_tiles(*tileset, map, *a.chunk, a.image->animated_cells, a.pos, tint,
                        ctx_ref<sprite_cache>(ctx)->tile_time);
}

// False when `spr` is certainly outside `view`. The box is a circle around the
// anchor that reaches the farthest corner, so rotation cannot pull it back in.
bool sprite_on_screen(const context &ctx, const transform &tr, const sprite &spr,
                      const rect &view) {
  const texture_slot *slot = texture_slot_of(ctx_ref<texture_store>(ctx), spr.texture);
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
  bool by_y = false; // the layer is sorted by y (draw_set_y_sort())
  u32 order = 0;     // place in the list, so the sort keeps ties in order
};

// What the world pass shows this frame: camera_bounds(), or with a shake on,
// the box round the shaken camera's view (camera.cpp keeps the camera it drew
// the world through), which reaches a little past the steady one.
rect frame_view(const context &ctx) {
  if (ctx_ref<fx_state>(ctx)->trauma <= 0.0f)
    return camera_bounds(ctx);
  const Camera2D &camera = ctx_ref<camera_post>(ctx)->world_camera;
  const vec2 screen = screen_size(ctx);
  const Vector2 corners[4] = {{0.0f, 0.0f}, {screen.x, 0.0f}, {0.0f, screen.y}, {screen.x, screen.y}};
  Vector2 lo = GetScreenToWorld2D(corners[0], camera);
  Vector2 hi = lo;
  for (const Vector2 &corner : corners) {
    const Vector2 p = GetScreenToWorld2D(corner, camera);
    lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
    hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
  }
  return rect{{lo.x, lo.y}, {hi.x - lo.x, hi.y - lo.y}};
}

void draw(context &ctx) {
  entt::registry &registry = ctx_ref<ecs_store>(ctx)->registry;
  render_stats &stats = ctx_ref<render_stats>(ctx);
  const rect view = frame_view(ctx);
  // Kept between frames: no allocation once it has grown.
  thread_local std::vector<draw_item> items;
  items.clear();
  for (auto [entity, tr, map] : registry.view<const transform, const tilemap>().each()) {
    if (map.visible)
      items.push_back({map.layer, 0, entity});
  }
  for (auto [entity, tr, spr] : registry.view<const transform, const sprite>().each()) {
    if (!spr.visible)
      continue;
    if (!sprite_on_screen(ctx, tr, spr, view)) {
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
    if (extent != nullptr && !particles_on_screen(tr, em, *extent, view)) {
      stats.emitters_culled++;
      continue;
    }
    items.push_back({em.layer, 2, entity, tr.pos.y});
  }
  // The y-sorted layers are looked up once per item, not per comparison, and
  // a plain sort with the list's order as the last key stands in for
  // stable_sort, which allocates a buffer every call.
  const std::unordered_set<i32> &by_y = ctx_ref<sprite_cache>(ctx)->y_sorted;
  for (usize i = 0; i < items.size(); i++) {
    items[i].order = (u32)i;
    items[i].by_y = !by_y.empty() && by_y.contains(items[i].layer);
  }
  std::sort(items.begin(), items.end(), [](const draw_item &a, const draw_item &b) {
    if (a.layer != b.layer)
      return a.layer < b.layer;
    // Tilemaps stay first, as the ground; the rest by y.
    if (a.by_y && a.kind != 0 && b.kind != 0 && a.y != b.y)
      return a.y < b.y;
    if (a.kind != b.kind)
      return a.kind < b.kind;
    return a.order < b.order;
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
    if (const texture_slot *slot = texture_slot_of(ctx_ref<texture_store>(ctx), spr.texture))
      stats.note_draw(slot->texture.id, blend_alpha);
    texture_store_draw_ex(ctx_ref<texture_store>(ctx), spr.texture,
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

void draw_portable_tilemap(context &ctx,const transform &tr,const tilemap &map) {
  const auto *slot=texture_slot_of(ctx_ref<texture_store>(ctx),map.tileset);
  if(!slot) return;
  if(!slot->portable) throw std::runtime_error("njin bgfx: tilemap has no backend texture");
  const vec2 step=map.tile_size+vec2{map.spacing,map.spacing};
  if(!std::isfinite(step.x) || !std::isfinite(step.y) || step.x<=0 || step.y<=0 ||
     map.tile_size.x<=0 || map.tile_size.y<=0 || !std::isfinite(map.margin) ||
     !std::isfinite(map.cell_size.x) || !std::isfinite(map.cell_size.y) ||
     !std::isfinite(map.hex_side) || map.hex_side<0 || map.orientation>tile_hexagonal)
    throw std::invalid_argument("njin bgfx: invalid tilemap dimensions");
  const double column_count=(double(texture_area(*slot).width)-2.0*map.margin+map.spacing)/step.x;
  if(!std::isfinite(column_count) || column_count>INT32_MAX)
    throw std::invalid_argument("njin bgfx: invalid tilemap source layout");
  const i32 columns=column_count>0 ? (i32)column_count:0;
  if(columns<=0) return;
  const auto view=camera_bounds(ctx);
  if(!std::isfinite(view.pos.x) || !std::isfinite(view.pos.y) ||
     !std::isfinite(view.size.x) || !std::isfinite(view.size.y))
    throw std::invalid_argument("njin bgfx: non-finite tilemap camera bounds");
  const vec2 cell_size=map.cell_size.x>0 && map.cell_size.y>0 ? map.cell_size:map.tile_size;
  const float rise=std::max(0.0f,map.tile_size.y-cell_size.y);
  const float wide=std::max(0.0f,map.tile_size.x-cell_size.x);
  const rect area{{view.pos.x-wide,view.pos.y},{view.size.x+wide,view.size.y+rise}};
  cell lo{INT32_MAX,INT32_MAX},hi{INT32_MIN,INT32_MIN};
  for(const auto point:{area.pos,area.pos+vec2{area.size.x,0},area.pos+vec2{0,area.size.y},area.pos+area.size}) {
    const auto c=tilemap_cell_at(map,tr.pos,point);
    lo={std::min(lo.x,c.x),std::min(lo.y,c.y)}; hi={std::max(hi.x,c.x),std::max(hi.y,c.y)};
  }
  // Bound CPU work explicitly when an extreme camera exposes enormous grids.
  const i64 count_x=i64(hi.x)-lo.x+3,count_y=i64(hi.y)-lo.y+3;
  if(lo.x==INT32_MIN || lo.y==INT32_MIN || hi.x==INT32_MAX || hi.y==INT32_MAX ||
     count_x<=0 || count_y<=0 || count_x>2000000 || count_y>2000000/count_x)
    throw std::runtime_error("njin bgfx: tilemap visible cell budget exceeded");
  --lo.x; --lo.y; ++hi.x; ++hi.y;
  thread_local std::vector<texture_draw_desc> tiles;
  tiles.clear();
  const float time=ctx_ref<sprite_cache>(ctx)->tile_time;
  const auto append=[&](i32 x,i32 y) {
    const i32 value=tilemap_get(map,x,y);
    if(value<0) return;
    i32 id=tile_id(value);
    if(const auto anim=map.anims.find(id);anim!=map.anims.end()) {
      const auto frame=tile_anim_frame(anim->second,time); if(frame>=0) id=frame;
    }
    const auto box=tilemap_cell_rect(map,tr.pos,x,y);
    const rect target{{box.pos.x,box.pos.y+box.size.y-map.tile_size.y},map.tile_size};
    if(!rects_overlap(target,view)) return;
    tiles.push_back({.pos=target.pos,
      .source={{map.margin+(id%columns)*step.x,map.margin+(id/columns)*step.y},map.tile_size},
      .flip_x=(value&tile_flip_x)!=0,.flip_y=(value&tile_flip_y)!=0,.tint=map.tint});
  };
  // Match the existing Tiled painter order, including tall overlapping tiles.
  if(map.orientation==tile_isometric) {
    for(i64 diagonal=i64(lo.x)+lo.y;diagonal<=i64(hi.x)+hi.y;++diagonal)
      for(i64 x=std::max(i64(lo.x),diagonal-hi.y);x<=std::min(i64(hi.x),diagonal-lo.y);++x)
        append((i32)x,(i32)(diagonal-x));
  } else if(map.orientation!=tile_orthogonal && map.stagger_x) {
    for(i64 y=lo.y;y<=hi.y;++y) for(bool lowered:{false,true})
      for(i64 x=lo.x;x<=hi.x;++x)
        if((((x&1)!=0)!=map.stagger_even)==lowered) append((i32)x,(i32)y);
  } else {
    for(i64 y=lo.y;y<=hi.y;++y) for(i64 x=lo.x;x<=hi.x;++x) append((i32)x,(i32)y);
  }
  draw_portable_slot_textures(ctx,*slot,tiles,true);
}

void draw_portable_particles(context &ctx,const transform &tr,const particle_emitter &em) {
  if(em.gpu) throw std::logic_error("njin bgfx: GPU emitter drawing requires its entity snapshot");
  if(em.blend!=blend_alpha && em.blend!=blend_additive && em.blend!=blend_multiply)
    throw std::invalid_argument("njin bgfx: invalid particle blend mode");
  const auto blend=em.blend==blend_additive ? RenderBlend::Additive:
    em.blend==blend_multiply ? RenderBlend::Multiply:RenderBlend::Alpha;
  const auto *slot=texture_slot_of(ctx_ref<texture_store>(ctx),em.texture);
  RenderTextureHandle texture{};
  vec2 image_size{},source_size{};
  if(slot) {
    texture=slot->portable; image_size={(float)slot->texture.width,(float)slot->texture.height};
    source_size=em.source.size.x==0 || em.source.size.y==0 ? vec2{texture_area(*slot).width,texture_area(*slot).height}:em.source.size;
    if(!texture) throw std::runtime_error("njin bgfx: particle has no backend texture");
  } else {
    const auto shape=static_cast<unsigned>(em.shape);
    auto &cache=ctx_ref<sprite_cache>(ctx)->particle_textures;
    if(shape>=cache.size()) throw std::invalid_argument("njin bgfx: invalid particle shape");
    auto &handle=cache[shape];
    const unsigned side=em.shape==particle_square ? 1:64;
    if(!handle) {
      std::vector<unsigned char> pixels(side*side*4,255);
      if(side>1) for(unsigned y=0;y<side;++y) for(unsigned x=0;x<side;++x) {
        const float dx=(x+0.5f)/side*2-1,dy=(y+0.5f)/side*2-1;
        const float fall=std::max(0.0f,1-dx*dx-dy*dy);
        pixels[(y*side+x)*4+3]=em.shape==particle_soft ? (unsigned char)(255*fall*fall):(fall>0 ? 255:0);
      }
      handle=renderer_create_texture(ctx,{side,side,pixels});
    }
    texture=handle; image_size=source_size={(float)side,(float)side};
  }
  const float longest=std::max(source_size.x,source_size.y);
  if(longest<=0) return;
  thread_local std::vector<texture_draw_desc> descriptions;
  descriptions.clear();
  for(const auto &p:em.particles) {
    const float t=p.age/p.life;
    const float size=lerp(em.size_start,em.size_end,t)*p.size;
    const auto tint=lerp(em.color_start,em.color_end,t);
    if(size<=0 || tint.a<=0) continue;
    const auto pos=em.local_space ? transform_combine(tr,{.pos=p.pos}).pos:p.pos;
    const float scale=size/longest;
    descriptions.push_back({.pos=pos,.source=slot ? em.source:rect{},.scale={scale,scale},
      .origin={0.5f,0.5f},.rotation=p.rot,.tint=tint});
  }
  if(slot) draw_portable_slot_textures(ctx,*slot,descriptions,true,blend);
  else draw_portable_textures(ctx,texture,image_size,descriptions,em.shape==particle_soft,true,blend);
}

void reset_portable_stats(context &ctx) { ctx_ref<render_stats>(ctx)->reset(); }
void draw_portable(context &ctx) {
  auto &registry=ctx_ref<ecs_store>(ctx)->registry;
  render_stats &stats=ctx_ref<render_stats>(ctx);
  // As the OpenGL renderer's draw: what is off screen is left out, and neighbours of one
  // texture (one atlas page) go to the GPU as one batch.
  const rect view=camera_bounds(ctx);
  thread_local std::vector<draw_item> entities;
  entities.clear();
  for(auto [entity,tr,map]:registry.view<const transform,const tilemap>().each())
    if(map.visible) entities.push_back({map.layer,0,entity,tr.pos.y});
  for(auto [entity,tr,spr]:registry.view<const transform,const sprite>().each()) {
    if(!spr.visible) continue;
    if(!sprite_on_screen(ctx,tr,spr,view)) {stats.sprites_culled++;continue;}
    entities.push_back({spr.layer,1,entity,tr.pos.y+spr.sort_offset});
  }
  for(auto [entity,tr,em]:registry.view<const transform,const particle_emitter>().each()) {
    if(!em.visible || em.particles.empty()) continue;
    const particle_extent *extent=registry.try_get<particle_extent>(entity);
    if(extent!=nullptr && !particles_on_screen(tr,em,*extent,view)) {stats.emitters_culled++;continue;}
    entities.push_back({em.layer,2,entity,tr.pos.y});
  }
  const auto &by_y=ctx_ref<sprite_cache>(ctx)->y_sorted;
  std::stable_sort(entities.begin(),entities.end(),[&](const auto &a,const auto &b) {
    if(a.layer!=b.layer) return a.layer<b.layer;
    if(by_y.contains(a.layer) && a.kind!=0 && b.kind!=0 && a.y!=b.y) return a.y<b.y;
    if(a.kind!=b.kind) return a.kind<b.kind;
    return entt::to_integral(a.entity)<entt::to_integral(b.entity);
  });
  thread_local std::vector<texture_draw_desc> batch;
  batch.clear();
  const texture_slot *batch_slot=nullptr;
  // A batch is one GPU texture and filter: the images of one atlas page share it, their
  // sources moved into the page (as draw_portable_slot_textures does), so they batch too.
  const auto flush=[&] {
    if(batch_slot && !batch.empty()) {
      const auto &s=*batch_slot;
      const auto min_filter=s.filter==filter_mipmap?RenderMinFilter::LinearMipLinear:s.filter==filter_linear?RenderMinFilter::LinearMipNearest:RenderMinFilter::NearestMipNearest;
      draw_portable_textures(ctx,s.portable,{float(s.texture.width),float(s.texture.height)},batch,s.filter!=filter_nearest,true,std::nullopt,min_filter);
      stats.batches++;
    }
    batch.clear();batch_slot=nullptr;
  };
  for(const auto &item:entities) {
    const auto entity=item.entity;
    if(item.kind==0) {
      flush();draw_portable_tilemap(ctx,registry.get<transform>(entity),registry.get<tilemap>(entity));
      stats.tile_chunks++;stats.batches++;continue;
    }
    if(item.kind==2) {
      flush();
      const auto &em=registry.get<particle_emitter>(entity);
      if(em.gpu) particles_gpu_draw(ctx,entity,registry.get<transform>(entity),em);
      else draw_portable_particles(ctx,registry.get<transform>(entity),em);
      stats.emitters++;stats.particles+=u32(em.particles.size());if(em.gpu) stats.particles_gpu+=u32(em.particles.size());
      stats.batches++;continue;
    }
    const auto &spr=registry.get<sprite>(entity);
    const auto &tr=registry.get<transform>(entity);
    const auto *slot=texture_slot_of(ctx_ref<texture_store>(ctx),spr.texture);
    if(!slot) continue;
    if(!slot->portable) throw std::runtime_error("njin bgfx: sprite has no backend texture");
    const auto *flash=registry.try_get<flash_fx>(entity);const auto *dissolve=registry.try_get<dissolve_fx>(entity);
    const texture_draw_desc desc{.pos=tr.pos,.source=spr.source,.scale={tr.scale,tr.scale},.origin=spr.origin,.rotation=tr.rot,
      .flip_x=spr.flip_x,.flip_y=spr.flip_y,.tint=spr.tint};
    stats.sprites++;
    if(!flash && !dissolve) {
      if(!batch_slot || slot->portable.id!=batch_slot->portable.id || slot->filter!=batch_slot->filter) {flush();batch_slot=slot;}
      batch.push_back(desc);
      if(slot->packed) {
        auto &d=batch.back();
        if(d.source.size.x==0||d.source.size.y==0) d.source={{0,0},{slot->area.width,slot->area.height}};
        d.source.pos+=vec2{slot->area.x,slot->area.y};
      }
      continue;
    }
    flush();stats.batches++;
    const auto effects=portable_sprite_effects(flash,dissolve);
    if(effects.dissolving&&effects.dissolve[0]>=1) continue;
    auto &state=ctx_ref<window_guard>(ctx).renderer_resources.draw_2d;
    struct Restore {RenderSpriteEffects &value,previous;~Restore(){value=previous;}} restore{state.sprite_effects,state.sprite_effects};
    if(flash||dissolve) state.sprite_effects=effects;
    draw_portable_slot_texture(ctx,*slot,desc,true);
  }
  flush();
}
void setup(context &ctx) {
  if(ctx_ref<config>(ctx).graphics_backend!=GraphicsBackend::OpenGL) {
    ecs_register(ctx,phase_post_update,animate,"animate");
    ecs_register(ctx,phase_post_update,fx_update_sprite_flashes,"fx_update_sprite_flashes");
    ecs_register(ctx,phase_post_update,fx_update_sprite_dissolves,"fx_update_sprite_dissolves");
    ecs_register(ctx,phase_pre_render,reset_portable_stats,"render_stats_reset");
    ecs_register(ctx,phase_render,draw_portable,"draw_portable");
    return;
  }
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

void draw_set_y_sort(context &ctx, i32 layer, bool on) {
  if (on)
    ctx_ref<sprite_cache>(ctx)->y_sorted.insert(layer);
  else
    ctx_ref<sprite_cache>(ctx)->y_sorted.erase(layer);
}

mod_desc sprite_module() {
  return mod_desc{.name = "njin.sprite", .setup = setup};
}
} // namespace njin

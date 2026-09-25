#include "njin_texture.h"
#include "njin2rl.h"
#include "njin_log.h"
#include "njin_path.h"
#include <string>

namespace njin {
namespace {
texture_slot *texture_slot_of(texture_store &store, texture_handle handle) {
  return const_cast<texture_slot *>(
      texture_slot_of(static_cast<const texture_store &>(store), handle));
}

render_texture_slot *render_texture_slot_of(render_texture_store &store,
                                            render_texture_handle handle) {
  return const_cast<render_texture_slot *>(render_texture_slot_of(
      static_cast<const render_texture_store &>(store), handle));
}
} // namespace

// Textures

texture_handle texture_store_load(texture_store &store, const char *path) {
  if (path == nullptr) {
    NJIN_WARN("texture: path is null");
    return texture_handle{};
  }
  // raylib only logs a missing file; fail loudly with the path instead.
  const std::string resolved = asset_path(path);
  if (!FileExists(resolved.c_str())) {
    NJIN_WARN("texture: file not found: %s", path);
    return texture_handle{};
  }

  const Texture2D texture = LoadTexture(resolved.c_str());
  if (!IsTextureValid(texture)) {
    NJIN_WARN("texture: failed to load: %s", path);
    return texture_handle{};
  }

  // raylib samples new textures with POINT; njin documents linear as the
  // default and leaves pixel art to opt in with texture_set_filter.
  SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
  store.slots.push_back(texture_slot{
      .texture = texture, .alive = true, .filter = filter_linear, .path = resolved, .version = 0});
  return texture_handle{.id = (u32)store.slots.size()};
}

void texture_store_unload(texture_store &store, texture_handle handle) {
  texture_slot *slot = texture_slot_of(store, handle);
  if (slot == nullptr)
    return;
  UnloadTexture(slot->texture);
  *slot = texture_slot{};
}

bool texture_store_reload(texture_store &store, texture_handle handle) {
  texture_slot *slot = texture_slot_of(store, handle);
  if (slot == nullptr || slot->path.empty())
    return false;
  const Texture2D texture = LoadTexture(slot->path.c_str());
  if (!IsTextureValid(texture)) {
    NJIN_WARN("texture: reload failed, keeping the old image: %s", slot->path.c_str());
    return false;
  }
  UnloadTexture(slot->texture);
  slot->texture = texture;
  SetTextureFilter(slot->texture, texture_filter_to_raylib(slot->filter));
  slot->version++;
  return true;
}

texture_store::~texture_store() {
  for (usize i = 0; i < slots.size(); i++)
    texture_store_unload(*this, texture_handle{.id = (u32)(i + 1)});
}

vec2 texture_store_size(const texture_store &store, texture_handle handle) {
  const texture_slot *slot = texture_slot_of(store, handle);
  if (slot == nullptr)
    return vec2{0.0f, 0.0f};
  return vec2{(f32)slot->texture.width, (f32)slot->texture.height};
}

void texture_store_draw(const texture_store &store, texture_handle handle,
                        vec2 pos, rgba tint) {
  const texture_slot *slot = texture_slot_of(store, handle);
  if (slot == nullptr)
    return;
  Vector2 position{};
  to_raylib(pos, position);
  Color color{};
  to_raylib(tint, color);
  DrawTextureV(slot->texture, position, color);
}

int texture_filter_to_raylib(texture_filter filter) {
  return filter == filter_nearest ? TEXTURE_FILTER_POINT
                                  : TEXTURE_FILTER_BILINEAR;
}

void texture_store_set_filter(texture_store &store, texture_handle handle,
                              texture_filter filter) {
  texture_slot *slot = texture_slot_of(store, handle);
  if (slot == nullptr)
    return;
  slot->filter = filter;
  SetTextureFilter(slot->texture, texture_filter_to_raylib(filter));
}

void texture_store_draw_ex(const texture_store &store, texture_handle handle,
                           const texture_draw_desc &desc) {
  const texture_slot *slot = texture_slot_of(store, handle);
  if (slot == nullptr)
    return;
  const Texture2D &texture = slot->texture;
  const bool whole = desc.source.size.x == 0.0f || desc.source.size.y == 0.0f;
  const f32 sw = whole ? (f32)texture.width : desc.source.size.x;
  const f32 sh = whole ? (f32)texture.height : desc.source.size.y;
  // A negative scale is a flip; DrawTexturePro expresses flips as a negative
  // source size and wants a positive destination.
  const bool flip_x = desc.flip_x != (desc.scale.x < 0.0f);
  const bool flip_y = desc.flip_y != (desc.scale.y < 0.0f);
  const f32 dw = sw * (desc.scale.x < 0.0f ? -desc.scale.x : desc.scale.x);
  const f32 dh = sh * (desc.scale.y < 0.0f ? -desc.scale.y : desc.scale.y);
  const Rectangle source{whole ? 0.0f : desc.source.pos.x,
                         whole ? 0.0f : desc.source.pos.y,
                         flip_x ? -sw : sw, flip_y ? -sh : sh};
  const Rectangle dest{desc.pos.x, desc.pos.y, dw, dh};
  const Vector2 origin{desc.origin.x * dw, desc.origin.y * dh};
  Color color{};
  to_raylib(desc.tint, color);
  DrawTexturePro(texture, source, dest, origin, desc.rotation, color);
}

// Render textures

render_texture_handle render_texture_store_load(render_texture_store &store,
                                                u32 width, u32 height) {
  if (width == 0 || height == 0) {
    NJIN_WARN("render texture: invalid size %ux%u", width, height);
    return render_texture_handle{};
  }

  const RenderTexture2D target = LoadRenderTexture((i32)width, (i32)height);
  if (!IsRenderTextureValid(target)) {
    // A partially created framebuffer may still own GPU objects.
    UnloadRenderTexture(target);
    NJIN_WARN("render texture: failed to create %ux%u", width, height);
    return render_texture_handle{};
  }

  store.slots.push_back(render_texture_slot{.target = target, .alive = true});
  return render_texture_handle{.id = (u32)store.slots.size()};
}

void render_texture_store_unload(render_texture_store &store,
                                 render_texture_handle handle) {
  render_texture_slot *slot = render_texture_slot_of(store, handle);
  if (slot == nullptr)
    return;
  UnloadRenderTexture(slot->target);
  *slot = render_texture_slot{};
}

render_texture_store::~render_texture_store() {
  for (usize i = 0; i < slots.size(); i++)
    render_texture_store_unload(*this,
                                render_texture_handle{.id = (u32)(i + 1)});
}

vec2 render_texture_store_size(const render_texture_store &store,
                               render_texture_handle handle) {
  const render_texture_slot *slot = render_texture_slot_of(store, handle);
  if (slot == nullptr)
    return vec2{0.0f, 0.0f};
  return vec2{(f32)slot->target.texture.width,
              (f32)slot->target.texture.height};
}

bool render_texture_store_begin(const render_texture_store &store,
                                render_texture_handle handle) {
  const render_texture_slot *slot = render_texture_slot_of(store, handle);
  if (slot == nullptr)
    return false;
  BeginTextureMode(slot->target);
  return true;
}

void render_texture_store_clear(rgba color) {
  Color clear{};
  to_raylib(color, clear);
  ClearBackground(clear);
}

void render_texture_store_end() { EndTextureMode(); }

void render_texture_store_draw(const render_texture_store &store,
                               render_texture_handle handle, vec2 pos,
                               rgba tint) {
  const render_texture_slot *slot = render_texture_slot_of(store, handle);
  if (slot == nullptr)
    return;
  const Texture2D &texture = slot->target.texture;
  // OpenGL framebuffers are stored bottom-up; a negative source height flips
  // the image so it appears the way it was drawn.
  const Rectangle source{0.0f, 0.0f, (f32)texture.width,
                         -(f32)texture.height};
  Vector2 position{};
  to_raylib(pos, position);
  Color color{};
  to_raylib(tint, color);
  DrawTextureRec(texture, source, position, color);
}
} // namespace njin

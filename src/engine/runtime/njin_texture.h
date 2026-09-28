#pragma once
#include "njin_internal_only.h"

#include "_types.h"
#include "njin_atlas.h"
#include "njin_draw.h"
#include <raylib.h>
#include <string>
#include <vector>

namespace njin {
// Both stores follow shader_store (njin_shader.h): handle id 0 is "invalid",
// id N maps to slots[N - 1], and slots are never reused, so a stale handle can
// never alias a newer resource.

struct texture_slot {
  Texture2D texture{};
  bool alive = false;
  // Kept so derived images (tilemap chunks) can be sampled the same way.
  texture_filter filter = filter_linear;
  // Resolved file path, for hot reload. Empty for textures not from a file.
  std::string path;
  // Bumped each time the texture is reloaded, so images derived from it
  // (tilemap chunks) know to redraw.
  u32 version = 0;
  // An image packed into an atlas page (atlas_load): `texture` is then the
  // page, shared and owned by the atlas, and `area` is where the image sits in
  // it. Always read the image through texture_area().
  bool packed = false;
  Rectangle area{};
  // Material shader (texture_set_shader): auto-bound by texture_store_draw()
  // and texture_store_draw_ex() so a game does not need shader_begin()/
  // shader_end() around every draw. Id 0 is "none". Not consulted by the ECS
  // sprite/tilemap/particle renderers, which manage their own shaders (e.g.
  // the flash/dissolve fx pass).
  shader_handle shader{};
};

// The part of `slot.texture` the image occupies: all of it for an ordinary
// texture, its rectangle in the page for a packed one. Sizes and source
// rectangles of drawing calls are relative to this, so callers add its origin.
inline Rectangle texture_area(const texture_slot &slot) {
  return slot.packed ? slot.area
                     : Rectangle{0.0f, 0.0f, (f32)slot.texture.width, (f32)slot.texture.height};
}

// One page of an atlas: a texture the images are packed into in rows.
struct atlas_page {
  struct shelf {
    i32 y = 0;
    i32 height = 0;
    i32 x = 0; // next free column
  };
  Texture2D texture{};
  std::vector<shelf> shelves;
  i32 next_y = 0; // top of the next new shelf
};

struct atlas_slot {
  bool alive = false;
  i32 size = 2048;
  i32 padding = 1;
  texture_filter filter = filter_linear;
  std::vector<atlas_page> pages;
  std::vector<u32> images; // texture slot ids packed here
};

// Owns the GPU textures of every live slot. The destructor frees them, so it
// must run while the GL context is still alive (before CloseWindow).
struct texture_store {
  std::vector<texture_slot> slots;
  std::vector<atlas_slot> atlases; // atlas N is atlases[N - 1], never reused

  texture_store() = default;
  ~texture_store();
  // Copying would free the same GPU textures twice.
  texture_store(const texture_store &) = delete;
  texture_store &operator=(const texture_store &) = delete;
};

struct render_texture_slot {
  RenderTexture2D target{};
  bool alive = false;
};

// Same ownership rules as texture_store, for framebuffers.
struct render_texture_store {
  std::vector<render_texture_slot> slots;

  render_texture_store() = default;
  ~render_texture_store();
  render_texture_store(const render_texture_store &) = delete;
  render_texture_store &operator=(const render_texture_store &) = delete;
};

// Atlases (njin_atlas.h). Images packed into one come back as ordinary texture
// slots with `packed` set.
atlas_handle atlas_store_create(texture_store &store, const atlas_desc &desc);
texture_handle atlas_store_load(texture_store &store, atlas_handle atlas, const char *path);
void atlas_store_destroy(texture_store &store, atlas_handle atlas);

inline const texture_slot *texture_slot_of(const texture_store &store,
                                           texture_handle handle) {
  if (handle.id == 0 || handle.id > store.slots.size())
    return nullptr;
  const texture_slot &slot = store.slots[handle.id - 1];
  return slot.alive ? &slot : nullptr;
}

inline const render_texture_slot *
render_texture_slot_of(const render_texture_store &store,
                       render_texture_handle handle) {
  if (handle.id == 0 || handle.id > store.slots.size())
    return nullptr;
  const render_texture_slot &slot = store.slots[handle.id - 1];
  return slot.alive ? &slot : nullptr;
}

// Returns an invalid handle if the file is missing or cannot be decoded.
texture_handle texture_store_load(texture_store &store, const char *path);
void texture_store_unload(texture_store &store, texture_handle handle);
// Reloads a texture from its file into the same slot. On failure the old
// texture stays and false is returned.
bool texture_store_reload(texture_store &store, texture_handle handle);
vec2 texture_store_size(const texture_store &store, texture_handle handle);
// Sets or clears (shader id 0) the texture's material shader, see
// texture_slot::shader. Only texture_draw()/texture_draw_ex() (njin_render.h)
// auto-bind it; the ECS sprite/tilemap/particle renderers draw straight from
// this store and manage their own shaders (e.g. the flash/dissolve fx pass),
// so a material shader would silently fight them.
void texture_store_set_shader(texture_store &store, texture_handle handle,
                              shader_handle shader);
void texture_store_draw(const texture_store &store, texture_handle handle,
                        vec2 pos, rgba tint);
void texture_store_draw_ex(const texture_store &store, texture_handle handle,
                           const texture_draw_desc &desc);
void texture_store_set_filter(texture_store &store, texture_handle handle,
                              texture_filter filter);
int texture_filter_to_raylib(texture_filter filter);

// Returns an invalid handle if the size is 0 or the framebuffer is incomplete.
render_texture_handle render_texture_store_load(render_texture_store &store,
                                                u32 width, u32 height);
void render_texture_store_unload(render_texture_store &store,
                                 render_texture_handle handle);
vec2 render_texture_store_size(const render_texture_store &store,
                               render_texture_handle handle);
// Returns false (and begins nothing) for an invalid handle.
bool render_texture_store_begin(const render_texture_store &store,
                                render_texture_handle handle);
// Fills the render texture being drawn into. Call between begin and end.
void render_texture_store_clear(rgba color);
void render_texture_store_end();
bool render_texture_store_save(render_texture_store &store, render_texture_handle handle, const char *path);
void render_texture_store_set_filter(render_texture_store &store, render_texture_handle handle,
                                     texture_filter filter);
void render_texture_store_draw(const render_texture_store &store,
                               render_texture_handle handle, vec2 pos,
                               rgba tint);
} // namespace njin

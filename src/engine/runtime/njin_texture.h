#pragma once

#include "_types.h"
#include <raylib.h>
#include <vector>

namespace njin {
// Both stores follow shader_store (njin_shader.h): handle id 0 is "invalid",
// id N maps to slots[N - 1], and slots are never reused, so a stale handle can
// never alias a newer resource.

struct texture_slot {
  Texture2D texture{};
  bool alive = false;
};

// Owns the GPU textures of every live slot. The destructor frees them, so it
// must run while the GL context is still alive (before CloseWindow).
struct texture_store {
  std::vector<texture_slot> slots;

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
vec2 texture_store_size(const texture_store &store, texture_handle handle);
void texture_store_draw(const texture_store &store, texture_handle handle,
                        vec2 pos, rgba tint);

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
void render_texture_store_draw(const render_texture_store &store,
                               render_texture_handle handle, vec2 pos,
                               rgba tint);
} // namespace njin

#pragma once
#include "njin_internal_only.h"

#include "_types.h"
#include <raylib.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace njin {
// A texture bound to a sampler2D uniform besides texture0 (shader_set_texture).
// It keeps the handle, not the GL id, so a reloaded texture is still the one
// sampled.
struct shader_texture_binding {
  std::string name;
  bool is_render = false; // `id` is a render_texture_handle, else a texture_handle
  u32 id = 0;
};

// raylib's batch has room for this many extra samplers (RL_DEFAULT_BATCH_MAX_TEXTURE_UNITS).
inline constexpr usize shader_max_textures = 4;

struct shader_slot {
  Shader shader{};
  bool alive = false;
  // Uniform name -> location. Querying the driver every frame is slow, so
  // locations are looked up once and cached (-1 is cached too). Mutable so
  // the cache fills from a const shader_slot (binding at shader_begin).
  mutable std::unordered_map<std::string, i32> uniforms;
  // Resolved stage paths, for hot reload. Empty for a stage raylib supplies.
  std::string vs_path;
  std::string fs_path;
  // In the order set; unit 1 + index when drawn.
  std::vector<shader_texture_binding> textures;
};

// Owns the GPU programs of every live slot. The destructor frees them, so it
// must run while the GL context is still alive (before CloseWindow).
struct shader_store {
  std::vector<shader_slot> slots;

  shader_store() = default;
  ~shader_store();
  // Copying would free the same GPU programs twice.
  shader_store(const shader_store &) = delete;
  shader_store &operator=(const shader_store &) = delete;
};

// Handle id 0 is "invalid"; id N maps to slots[N - 1]. Slots are never reused,
// so a stale handle can never alias a newer shader.
inline const shader_slot *shader_slot_of(const shader_store &store,
                                         shader_handle handle) {
  if (handle.id == 0 || handle.id > store.slots.size())
    return nullptr;
  const shader_slot &slot = store.slots[handle.id - 1];
  return slot.alive ? &slot : nullptr;
}

inline shader_slot *shader_slot_of(shader_store &store, shader_handle handle) {
  return const_cast<shader_slot *>(
      shader_slot_of(static_cast<const shader_store &>(store), handle));
}

// Either path may be nullptr to keep raylib's default stage. Returns an invalid
// handle if a file is missing or the program fails to compile/link.
shader_handle shader_store_load(shader_store &store, const char *vspath,
                                const char *fspath);
void shader_store_unload(shader_store &store, shader_handle handle);
// Recompiles a shader from its files into the same slot. On a compile error
// the old program stays and false is returned.
bool shader_store_reload(shader_store &store, shader_handle handle);

void shader_store_begin(const shader_store &store, shader_handle handle);
void shader_store_end();

void shader_store_set_i32(shader_store &store, shader_handle handle,
                          const char *name, i32 value);
void shader_store_set_f32(shader_store &store, shader_handle handle,
                          const char *name, f32 value);
void shader_store_set_vec2(shader_store &store, shader_handle handle,
                           const char *name, vec2 value);
void shader_store_set_rgba(shader_store &store, shader_handle handle, const char *name, vec4 value);
void shader_store_set_vec4_array(shader_store &store, shader_handle handle, const char *name,
                                 const vec4 *values, u32 count);
// Binds `name` to a texture (or render texture) besides texture0. A name set
// again replaces its texture. Packed atlas images are refused: a sampler would
// see the whole page, not the image.
void shader_store_set_texture(shader_store &store, shader_handle handle, const char *name,
                              bool is_render, u32 id);

struct njin_ctx;
// Attach the shader's extra textures for the draws that follow. Both need the
// shader to be the active one. The first is for raylib's batch (the sampler
// lives until the batch is flushed: on the next full batch, 256 texture
// changes, or an instanced draw / render texture switch in between). The
// second is for draw_instanced, which draws itself: units 1.., undone by the
// third.
void shader_bind_textures(const njin_ctx &ctx, const shader_slot &slot);
void shader_bind_textures_instanced(const njin_ctx &ctx, const shader_slot &slot);
void shader_unbind_textures_instanced(const shader_slot &slot);
} // namespace njin

#pragma once

#include "_types.h"
#include <raylib.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace njin {
struct shader_slot {
  Shader shader{};
  bool alive = false;
  // Uniform name -> location. Querying the driver every frame is slow, so
  // locations are looked up once and cached (-1 is cached too).
  std::unordered_map<std::string, i32> uniforms;
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

void shader_store_begin(const shader_store &store, shader_handle handle);
void shader_store_end();

void shader_store_set_i32(shader_store &store, shader_handle handle,
                          const char *name, i32 value);
void shader_store_set_f32(shader_store &store, shader_handle handle,
                          const char *name, f32 value);
void shader_store_set_vec2(shader_store &store, shader_handle handle,
                           const char *name, vec2 value);
void shader_store_set_rgba(shader_store &store, shader_handle handle, const char *name, vec4 value);
} // namespace njin

#pragma once
#include "njin_internal_only.h"

#include "_types.h"
#include <array>
#include <unordered_map>
#include <vector>

namespace njin {
// One instance buffer: a vertex array with the shared unit quad at attribute 0
// and a buffer of per-instance floats that draw_instanced points the shader's
// `instance0..3` attributes at.
struct instance_slot {
  unsigned int vao = 0;
  unsigned int vbo = 0;
  u32 floats = 0;   // per instance: 4, 8, 12 or 16
  i32 capacity = 0; // bytes in `vbo`
  u32 count = 0;    // instances written by the last upload
  bool alive = false;
  // A copy of the last upload, kept only while the debug server runs, so the
  // inspector's 3D view can place what draw_instanced3d drew.
  std::vector<f32> cpu;
};

// Owns every instance buffer and the quad they share. The destructor frees the
// GL objects, so it must run while the GL context is still alive (context
// declares it after the window).
struct instance_store {
  std::vector<instance_slot> slots;
  unsigned int quad_vbo = 0;
  bool probed = false;
  bool available = false;
  // Shader program id -> locations of instance0..3 (-1 when a shader does not
  // declare one). Looked up once per program; a reloaded shader is a new id.
  std::unordered_map<unsigned int, std::array<int, 4>> locations;

  instance_store() = default;
  ~instance_store();
  instance_store(const instance_store &) = delete;
  instance_store &operator=(const instance_store &) = delete;
};

// Handle id 0 is "invalid"; id N maps to slots[N - 1]. Slots are never reused.
inline instance_slot *instance_slot_of(instance_store &store, instance_buffer_handle handle) {
  if (handle.id == 0 || handle.id > store.slots.size())
    return nullptr;
  instance_slot &slot = store.slots[handle.id - 1];
  return slot.alive ? &slot : nullptr;
}
} // namespace njin

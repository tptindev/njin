#include "njin_shader.h"

namespace njin {
namespace {
// raylib silently falls back to its default stage when a shader file cannot be
// read, which turns a typo in a path into a "working" pass-through shader.
// Check up front so a bad path fails loudly instead.
bool stage_readable(const char *path, const char *stage) {
  if (path == nullptr || FileExists(path))
    return true;
  TraceLog(LOG_WARNING, "SHADER: %s shader not found: %s", stage, path);
  return false;
}

i32 uniform_loc(shader_slot &slot, const char *name) {
  const auto it = slot.uniforms.find(name);
  if (it != slot.uniforms.end())
    return it->second;

  const i32 loc = GetShaderLocation(slot.shader, name);
  if (loc < 0) {
    TraceLog(LOG_WARNING, "SHADER: [ID %u] uniform '%s' not found",
             slot.shader.id, name);
  }
  slot.uniforms.emplace(name, loc);
  return loc;
}

void set_uniform(shader_store &store, shader_handle handle, const char *name,
                 const void *value, ShaderUniformDataType type) {
  if (name == nullptr)
    return;
  shader_slot *slot = shader_slot_of(store, handle);
  if (slot == nullptr)
    return;
  const i32 loc = uniform_loc(*slot, name);
  if (loc >= 0)
    SetShaderValue(slot->shader, loc, value, type);
}
} // namespace

shader_handle shader_store_load(shader_store &store, const char *vspath,
                                const char *fspath) {
  if (vspath == nullptr && fspath == nullptr) {
    TraceLog(LOG_WARNING, "SHADER: nothing to load, both paths are null");
    return shader_handle{};
  }
  if (!stage_readable(vspath, "vertex") ||
      !stage_readable(fspath, "fragment"))
    return shader_handle{};

  const Shader shader = LoadShader(vspath, fspath);
  if (!IsShaderValid(shader)) {
    // Even a failed load allocates the locations array; release it.
    UnloadShader(shader);
    return shader_handle{};
  }

  store.slots.push_back(
      shader_slot{.shader = shader, .alive = true, .uniforms = {}});
  return shader_handle{.id = (u32)store.slots.size()};
}

void shader_store_unload(shader_store &store, shader_handle handle) {
  shader_slot *slot = shader_slot_of(store, handle);
  if (slot == nullptr)
    return;
  UnloadShader(slot->shader);
  *slot = shader_slot{};
}

shader_store::~shader_store() {
  for (usize i = 0; i < slots.size(); i++)
    shader_store_unload(*this, shader_handle{.id = (u32)(i + 1)});
}

void shader_store_begin(shader_store &store, shader_handle handle) {
  const shader_slot *slot = shader_slot_of(store, handle);
  if (slot != nullptr)
    BeginShaderMode(slot->shader);
}

void shader_store_end() { EndShaderMode(); }

void shader_store_set_i32(shader_store &store, shader_handle handle,
                          const char *name, i32 value) {
  set_uniform(store, handle, name, &value, SHADER_UNIFORM_INT);
}

void shader_store_set_f32(shader_store &store, shader_handle handle,
                          const char *name, f32 value) {
  set_uniform(store, handle, name, &value, SHADER_UNIFORM_FLOAT);
}

void shader_store_set_vec2(shader_store &store, shader_handle handle,
                           const char *name, vec2 value) {
  const f32 data[2] = {value.x, value.y};
  set_uniform(store, handle, name, data, SHADER_UNIFORM_VEC2);
}

void shader_store_set_rgba(shader_store &store, shader_handle handle, const char *name, vec4 value) {
  const f32 data[4] = {value.x, value.y, value.z, value.w};
  set_uniform(store, handle, name, data, SHADER_UNIFORM_VEC4);
}
} // namespace njin

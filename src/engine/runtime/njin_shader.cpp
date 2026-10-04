#include "njin_shader.h"
#include "njin_ctx_impl.h"
#include "njin_log.h"
#include "njin_path.h"
#include <rlgl.h>
#include <string>

namespace njin {
namespace {
// raylib silently falls back to its default stage when a shader file cannot be
// read, which turns a typo in a path into a "working" pass-through shader.
// Check up front so a bad path fails loudly instead.
bool stage_readable(const char *path, const char *stage) {
  if (path == nullptr || FileExists(path))
    return true;
  NJIN_WARN("shader: %s shader not found: %s", stage, path);
  return false;
}

i32 uniform_loc(const shader_slot &slot, const char *name, bool warn = true) {
  const auto it = slot.uniforms.find(name);
  if (it != slot.uniforms.end())
    return it->second;

  const i32 loc = GetShaderLocation(slot.shader, name);
  if (loc < 0 && warn) {
    NJIN_WARN("shader: [ID %u] uniform '%s' not found", slot.shader.id, name);
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

// The GL texture behind a binding, or 0 when the handle is stale (unloaded) or
// names an image packed into an atlas.
u32 gl_id_of(const context &ctx, const shader_texture_binding &b) {
  if (b.is_render) {
    const render_texture_slot *rt = render_texture_slot_of(ctx.render_texture, render_texture_handle{.id = b.id});
    return rt != nullptr ? rt->target.texture.id : 0;
  }
  const texture_slot *t = texture_slot_of(ctx.texture, texture_handle{.id = b.id});
  return t != nullptr && !t->packed ? t->texture.id : 0;
}
} // namespace

void shader_slot_set_optional_vec3(const shader_slot &slot, const char *name, vec3 value) {
  const i32 loc = uniform_loc(slot, name, false);
  if (loc < 0)
    return;
  const f32 v[3] = {value.x, value.y, value.z};
  SetShaderValue(slot.shader, loc, v, SHADER_UNIFORM_VEC3);
}

void shader_slot_set_optional_f32(const shader_slot &slot, const char *name, f32 value) {
  const i32 loc = uniform_loc(slot, name, false);
  if (loc >= 0)
    SetShaderValue(slot.shader, loc, &value, SHADER_UNIFORM_FLOAT);
}

void shader_slot_set_optional_i32(const shader_slot &slot, const char *name, i32 value) {
  const i32 loc = uniform_loc(slot, name, false);
  if (loc >= 0)
    SetShaderValue(slot.shader, loc, &value, SHADER_UNIFORM_INT);
}

shader_handle shader_store_load(shader_store &store, const char *vspath,
                                const char *fspath) {
  if (vspath == nullptr && fspath == nullptr) {
    NJIN_WARN("shader: nothing to load, both paths are null");
    return shader_handle{};
  }
  const std::string vs = vspath != nullptr ? asset_path(vspath) : std::string{};
  const std::string fs = fspath != nullptr ? asset_path(fspath) : std::string{};
  vspath = vspath != nullptr ? vs.c_str() : nullptr;
  fspath = fspath != nullptr ? fs.c_str() : nullptr;
  if (!stage_readable(vspath, "vertex") ||
      !stage_readable(fspath, "fragment"))
    return shader_handle{};

  const Shader shader = LoadShader(vspath, fspath);
  if (!IsShaderValid(shader)) {
    // Even a failed load allocates the locations array; release it.
    UnloadShader(shader);
    return shader_handle{};
  }

  store.slots.push_back(shader_slot{
      .shader = shader, .alive = true, .uniforms = {}, .vs_path = vs, .fs_path = fs, .textures = {}});
  return shader_handle{.id = (u32)store.slots.size()};
}

bool shader_store_reload(shader_store &store, shader_handle handle) {
  shader_slot *slot = shader_slot_of(store, handle);
  if (slot == nullptr || (slot->vs_path.empty() && slot->fs_path.empty()))
    return false;
  const char *vs = slot->vs_path.empty() ? nullptr : slot->vs_path.c_str();
  const char *fs = slot->fs_path.empty() ? nullptr : slot->fs_path.c_str();
  const Shader shader = LoadShader(vs, fs);
  if (!IsShaderValid(shader)) {
    // The compiler's message is already in the log (raylib reports it).
    UnloadShader(shader);
    NJIN_WARN("shader: reload failed, keeping the old shader: %s", fs != nullptr ? fs : vs);
    return false;
  }
  UnloadShader(slot->shader);
  slot->shader = shader;
  // Locations belong to the old program.
  slot->uniforms.clear();
  return true;
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

void shader_store_begin(const shader_store &store, shader_handle handle) {
  const shader_slot *slot = shader_slot_of(store, handle);
  if (slot == nullptr)
    return;
  BeginShaderMode(slot->shader);
  store.active = handle;
}

void shader_store_end(const shader_store &store) {
  EndShaderMode();
  store.active = shader_handle{};
}

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

void shader_store_set_vec3(shader_store &store, shader_handle handle, const char *name, vec3 value) {
  const f32 data[3] = {value.x, value.y, value.z};
  set_uniform(store, handle, name, data, SHADER_UNIFORM_VEC3);
}

void shader_store_set_rgba(shader_store &store, shader_handle handle, const char *name, vec4 value) {
  const f32 data[4] = {value.x, value.y, value.z, value.w};
  set_uniform(store, handle, name, data, SHADER_UNIFORM_VEC4);
}
} // namespace njin

namespace njin {
void shader_store_set_vec4_array(shader_store &store, shader_handle handle, const char *name,
                                 const vec4 *values, u32 count) {
  static_assert(sizeof(vec4) == 4 * sizeof(f32), "a vec4 array is uploaded as it lies in memory");
  if (name == nullptr || values == nullptr || count == 0)
    return;
  shader_slot *slot = shader_slot_of(store, handle);
  if (slot == nullptr)
    return;
  const i32 loc = uniform_loc(*slot, name);
  if (loc >= 0)
    SetShaderValueV(slot->shader, loc, values, SHADER_UNIFORM_VEC4, (int)count);
}

void shader_store_set_texture(shader_store &store, shader_handle handle, const char *name,
                              bool is_render, u32 id) {
  if (name == nullptr)
    return;
  shader_slot *slot = shader_slot_of(store, handle);
  if (slot == nullptr)
    return;
  for (shader_texture_binding &b : slot->textures) {
    if (b.name == name) {
      b.is_render = is_render;
      b.id = id;
      return;
    }
  }
  if (slot->textures.size() >= shader_max_textures) {
    NJIN_WARN("shader: [ID %u] '%s' ignored, a shader takes at most %zu extra textures", slot->shader.id,
              name, shader_max_textures);
    return;
  }
  slot->textures.push_back(shader_texture_binding{.name = name, .is_render = is_render, .id = id});
}

void shader_bind_textures(const context &ctx, const shader_slot &slot) {
  for (const shader_texture_binding &b : slot.textures) {
    const u32 gl = gl_id_of(ctx, b);
    const i32 loc = uniform_loc(slot, b.name.c_str());
    if (gl != 0 && loc >= 0) {
      Texture2D texture{};
      texture.id = gl; // SetShaderValueTexture reads only the id
      SetShaderValueTexture(slot.shader, loc, texture);
    }
  }
}

void shader_bind_textures_instanced(const context &ctx, const shader_slot &slot) {
  int unit = 1;
  for (const shader_texture_binding &b : slot.textures) {
    const u32 gl = gl_id_of(ctx, b);
    const i32 loc = uniform_loc(slot, b.name.c_str());
    if (gl != 0 && loc >= 0) {
      rlActiveTextureSlot(unit);
      rlEnableTexture(gl);
      rlSetUniform(loc, &unit, RL_SHADER_UNIFORM_INT, 1);
    }
    unit++;
  }
  rlActiveTextureSlot(0);
}

void shader_bind_textures_from(const context &ctx, const shader_slot &slot, i32 first_unit) {
  rlEnableShader(slot.shader.id);
  i32 unit = first_unit;
  for (const shader_texture_binding &b : slot.textures) {
    const u32 gl = gl_id_of(ctx, b);
    const i32 loc = uniform_loc(slot, b.name.c_str());
    if (gl != 0 && loc >= 0) {
      rlActiveTextureSlot(unit);
      rlEnableTexture(gl);
      rlSetUniform(loc, &unit, RL_SHADER_UNIFORM_INT, 1);
    }
    unit++;
  }
  rlActiveTextureSlot(0);
}

void shader_unbind_textures_instanced(const shader_slot &slot) {
  for (usize i = 0; i < slot.textures.size(); i++) {
    rlActiveTextureSlot((int)i + 1);
    rlDisableTexture();
  }
  rlActiveTextureSlot(0);
}
} // namespace njin

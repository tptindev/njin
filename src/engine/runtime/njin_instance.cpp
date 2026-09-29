// Instanced drawing for games: a buffer of per-instance floats the game fills,
// and one rlDrawVertexArrayInstanced call that draws a unit quad per instance
// through the game's own shader. The GPU particles (modules/particles_gpu.cpp)
// do the same with a fixed shader; this is the general form.
#include "njin_instance.h"

#include "njin_ctx_impl.h"
#include "njin_log.h"
#include "njin_render.h"
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <cstdio>

namespace njin {
namespace {
// Two triangles covering the unit square, y down: what draw_instanced() tells
// the game's vertex shader `vertexPosition.xy` holds.
constexpr f32 quad_corners[12] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
                                  0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};

bool probe(instance_store &store) {
  if (store.probed)
    return store.available;
  store.probed = true;
  const int version = rlGetVersion();
  if (version != RL_OPENGL_33 && version != RL_OPENGL_43 && version != RL_OPENGL_ES_30) {
    NJIN_INFO("instancing: this OpenGL has none, draw_instanced draws nothing");
    return false;
  }
  store.quad_vbo = rlLoadVertexBuffer(quad_corners, (int)sizeof quad_corners, false);
  store.available = store.quad_vbo != 0;
  return store.available;
}

const std::array<int, 4> &locations_of(instance_store &store, unsigned int program) {
  auto it = store.locations.find(program);
  if (it != store.locations.end())
    return it->second;
  std::array<int, 4> locs{};
  for (int i = 0; i < 4; i++) {
    char name[16];
    std::snprintf(name, sizeof name, "instance%d", i);
    locs[(usize)i] = rlGetLocationAttrib(program, name);
  }
  return store.locations.emplace(program, locs).first->second;
}
} // namespace

instance_store::~instance_store() {
  for (instance_slot &slot : slots) {
    if (slot.vbo != 0)
      rlUnloadVertexBuffer(slot.vbo);
    if (slot.vao != 0)
      rlUnloadVertexArray(slot.vao);
  }
  if (quad_vbo != 0)
    rlUnloadVertexBuffer(quad_vbo);
}

bool instancing_available(const njin_ctx &ctx) {
  // Probing only reads the GL version and makes the shared quad once.
  return probe(const_cast<njin_ctx &>(ctx).instances);
}

instance_buffer_handle instance_buffer_create(njin_ctx &ctx, u32 floats_per_instance) {
  instance_store &store = ctx.instances;
  if (floats_per_instance == 0 || floats_per_instance > 16 || floats_per_instance % 4 != 0) {
    NJIN_WARN("instance_buffer_create: floats_per_instance must be 4, 8, 12 or 16, not %u",
              floats_per_instance);
    return {};
  }
  if (!probe(store))
    return {};
  instance_slot slot;
  slot.floats = floats_per_instance;
  slot.vao = rlLoadVertexArray();
  rlEnableVertexArray(slot.vao);
  rlEnableVertexBuffer(store.quad_vbo);
  rlSetVertexAttribute(RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION, 2, RL_FLOAT, false, 0, 0);
  rlEnableVertexAttribute(RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION);
  rlDisableVertexArray();
  slot.alive = true;
  store.slots.push_back(slot);
  return instance_buffer_handle{(u32)store.slots.size()};
}

void instance_buffer_destroy(njin_ctx &ctx, instance_buffer_handle handle) {
  instance_slot *slot = instance_slot_of(ctx.instances, handle);
  if (slot == nullptr)
    return;
  if (slot->vbo != 0)
    rlUnloadVertexBuffer(slot->vbo);
  if (slot->vao != 0)
    rlUnloadVertexArray(slot->vao);
  *slot = instance_slot{};
}

void instance_buffer_upload(njin_ctx &ctx, instance_buffer_handle handle, const f32 *data,
                            u32 count) {
  instance_slot *slot = instance_slot_of(ctx.instances, handle);
  if (slot == nullptr)
    return;
  slot->count = data != nullptr ? count : 0;
  if (slot->count == 0)
    return;
  const i32 bytes = (i32)(count * slot->floats * sizeof(f32));
  if (slot->vbo == 0 || bytes > slot->capacity) {
    // Grow by half again so a crowd that keeps growing does not reallocate
    // every frame. A new buffer is not bound to any attribute yet: draw does it.
    const i32 wanted = std::max({bytes, slot->capacity + slot->capacity / 2, 16 * 1024});
    if (slot->vbo != 0)
      rlUnloadVertexBuffer(slot->vbo);
    slot->vbo = rlLoadVertexBuffer(nullptr, wanted, true);
    slot->capacity = wanted;
  }
  rlUpdateVertexBuffer(slot->vbo, data, bytes, 0);
  if (ctx.debug.running)
    slot->cpu.assign(data, data + (usize)count * slot->floats);
  else
    slot->cpu.clear();
}

namespace {
// `texture_id` is bound to texture0 when not 0.
void draw_instanced_impl(njin_ctx &ctx, instance_buffer_handle handle, shader_handle shader,
                         u32 first, u32 count, unsigned int texture_id) {
  instance_store &store = ctx.instances;
  instance_slot *slot = instance_slot_of(store, handle);
  const shader_slot *program = shader_slot_of(ctx.shader, shader);
  if (slot == nullptr || program == nullptr || slot->vbo == 0 || first >= slot->count)
    return;
  count = std::min(count, slot->count - first);
  if (count == 0)
    return;

  render_stats &stats = ctx.stats;
  stats.instanced_calls++;
  stats.batches++; // the instanced call itself, and it ends the batch before it
  stats.note_flush();
  // What was queued before must reach the screen first, or it would draw on top.
  rlDrawRenderBatchActive();

  const unsigned int id = program->shader.id;
  rlEnableShader(id);
  const Matrix mvp = MatrixMultiply(
      MatrixMultiply(rlGetMatrixTransform(), rlGetMatrixModelview()), rlGetMatrixProjection());
  rlSetUniformMatrix(rlGetLocationUniform(id, "mvp"), mvp);
  if (texture_id != 0) {
    const int unit = 0;
    rlActiveTextureSlot(unit);
    rlEnableTexture(texture_id);
    rlSetUniform(rlGetLocationUniform(id, "texture0"), &unit, RL_SHADER_UNIFORM_INT, 1);
  }
  // Extra samplers of the shader (shader_set_texture), on units 1 and up.
  shader_bind_textures_instanced(ctx, *program);

  // Point this shader's instance attributes at the buffer, starting at
  // `first`. Pointing them per draw (not once per buffer) lets any shader and
  // any range use the same buffer.
  rlEnableVertexArray(slot->vao);
  rlEnableVertexBuffer(slot->vbo);
  const int stride = (int)(slot->floats * sizeof(f32));
  const std::array<int, 4> &locs = locations_of(store, id);
  for (u32 i = 0; i < slot->floats / 4; i++) {
    const int loc = locs[i];
    if (loc < 0)
      continue;
    rlSetVertexAttribute((unsigned int)loc, 4, RL_FLOAT, false, stride,
                         (int)(first * (u32)stride + i * 4 * sizeof(f32)));
    rlEnableVertexAttribute((unsigned int)loc);
    rlSetVertexAttributeDivisor((unsigned int)loc, 1);
  }

  // The camera's projection may mirror the triangles; they are not culled.
  rlDisableBackfaceCulling();
  rlDrawVertexArrayInstanced(0, 6, (int)count);
  rlEnableBackfaceCulling();
  rlDisableVertexArray();
  shader_unbind_textures_instanced(*program);
  if (texture_id != 0)
    rlDisableTexture();
  rlDisableShader();
}
} // namespace

void draw_instanced(njin_ctx &ctx, instance_buffer_handle handle, shader_handle shader, u32 first,
                    u32 count) {
  draw_instanced_impl(ctx, handle, shader, first, count, 0);
}

void draw_instanced(njin_ctx &ctx, instance_buffer_handle handle, shader_handle shader, u32 first,
                    u32 count, texture_handle texture) {
  const texture_slot *slot = texture_slot_of(ctx.texture, texture);
  if (slot != nullptr)
    draw_instanced_impl(ctx, handle, shader, first, count, slot->texture.id);
}

void draw_instanced(njin_ctx &ctx, instance_buffer_handle handle, shader_handle shader, u32 first,
                    u32 count, render_texture_handle texture) {
  const render_texture_slot *slot = render_texture_slot_of(ctx.render_texture, texture);
  if (slot != nullptr)
    draw_instanced_impl(ctx, handle, shader, first, count, slot->target.texture.id);
}
} // namespace njin

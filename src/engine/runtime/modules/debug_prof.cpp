#include "debug_prof.h"
#include "_comps.h"
#include "_tilemap.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_particles.h"
#include <algorithm>
#include <cmath>
#include <raylib.h>
#include <string>

namespace njin {
namespace {
constexpr usize per_component_overhead = debug_component_overhead;

f64 round2(f64 v) { return std::round(v * 100.0) / 100.0; }

usize json_bytes(const json_value &v) {
  usize n = sizeof(json_value);
  n += v.str.capacity();
  for (const json_value &item : v.items)
    n += json_bytes(item);
  for (const auto &[k, child] : v.members)
    n += k.capacity() + json_bytes(child);
  return n;
}

// Texture memory on the GPU, mip chain included.
usize texture_bytes(const Texture2D &t) {
  if (t.id == 0 || t.width <= 0 || t.height <= 0)
    return 0;
  const usize base = (usize)GetPixelDataSize(t.width, t.height, t.format);
  return t.mipmaps > 1 ? base + base / 3 : base;
}

// A render texture: colour attachment plus the depth renderbuffer (4 bytes).
usize target_bytes(const RenderTexture2D &t) {
  if (!IsRenderTextureValid(t))
    return 0;
  return texture_bytes(t.texture) + (usize)t.texture.width * (usize)t.texture.height * 4;
}

// Decoded audio held in memory.
usize audio_bytes(const AudioStream &s, usize frames) {
  return frames * (usize)s.channels * (usize)(s.sampleSize / 8);
}

json_value item(const char *kind, const std::string &name, usize bytes, bool gpu, const std::string &info = {}) {
  return json_value::make_object()
      .set("k", kind)
      .set("n", name)
      .set("b", (i64)bytes)
      .set("gpu", gpu)
      .set("i", info);
}

std::string dim(i32 w, i32 h) { return std::to_string(w) + " x " + std::to_string(h); }

usize chunk_bytes_of(const context &ctx, entt::entity e) {
  const auto it = ctx.sprites.chunks.find(e);
  if (it == ctx.sprites.chunks.end())
    return 0;
  usize n = 0;
  for (const auto &[key, image] : it->second)
    n += target_bytes(image.target);
  return n;
}
} // namespace

json_value debug_build_prof(const context &ctx) {
  static const char *phase_names[phase_count] = {"startup",    "pre_update", "fixed_update", "update", "post_update",
                                                 "pre_render", "render",     "post_render",  "shutdown"};
  const ecs_store &ecs = ctx.ecs;
  json_value phases = json_value::make_array();
  json_value systems = json_value::make_array();
  for (i32 p = 0; p < phase_count; p++) {
    phases.push(json_value::make_object()
                    .set("n", phase_names[p])
                    .set("ms", round2(ecs.phase_last[p])));
    for (const scheduled_system &sys : ecs.schedule[p]) {
      systems.push(json_value::make_object()
                       .set("p", p)
                       .set("n", sys.label)
                       .set("ms", round2(sys.stat.last))
                       .set("avg", round2(sys.stat.avg))
                       .set("peak", round2(sys.stat.peak))
                       .set("calls", (i64)sys.stat.calls));
    }
  }
  return json_value::make_object()
      .set("t", "prof")
      .set("frame", round2(ctx.time.dt_real * 1000.0f))
      .set("phases", std::move(phases))
      .set("systems", std::move(systems));
}

json_value debug_build_mem(const context &ctx) {
  const debug_state &d = ctx.debug;
  const entt::registry &reg = ctx.ecs.registry;
  json_value types = json_value::make_array();
  usize total = 0;
  for (auto [id, pool] : reg.storage()) {
    if (pool.info().hash() == entt::type_hash<entt::entity>::value() || pool.empty())
      continue;
    const auto it = d.types.find(pool.info().hash());
    const usize count = pool.size();
    const usize each = it != d.types.end() ? it->second.bytes : 0;
    usize heap = 0;
    if (it != d.types.end() && it->second.heap)
      for (const entt::entity e : pool)
        heap += it->second.heap(reg, e);
    const usize bytes = count * (each + per_component_overhead) + heap;
    total += bytes;
    std::string name(pool.info().name());
    if (it != d.types.end())
      name = it->second.name;
    else
      for (const char *prefix : {"njin::", "{anonymous}::", "(anonymous namespace)::", "`anonymous namespace'::"})
        if (name.rfind(prefix, 0) == 0)
          name.erase(0, std::string(prefix).size());
    types.push(json_value::make_object()
                   .set("n", name)
                   .set("count", (i64)count)
                   .set("size", (i64)each)
                   .set("known", it != d.types.end() && (each > 0 || it->second.heap != nullptr))
                   .set("heap", (i64)heap)
                   .set("bytes", (i64)bytes));
  }
  const usize entities = reg.view<entt::entity>().size();
  return json_value::make_object()
      .set("t", "mem")
      .set("types", std::move(types))
      .set("components_bytes", (i64)total)
      .set("entities", (i64)entities)
      .set("registry_bytes", (i64)(entities * sizeof(entt::entity)));
}

json_value debug_build_res(const context &ctx) {
  json_value items = json_value::make_array();
  usize gpu = 0, ram = 0;
  const auto add = [&](json_value v) {
    const usize b = (usize)v["b"].number_or(0.0);
    (v["gpu"].bool_or(false) ? gpu : ram) += b;
    items.push(std::move(v));
  };

  for (usize i = 0; i < ctx.texture.slots.size(); i++) {
    const texture_slot &s = ctx.texture.slots[i];
    if (!s.alive || s.packed) // a packed image is part of its atlas page, listed below
      continue;
    const std::string name = s.path.empty() ? "texture #" + std::to_string(i + 1) : s.path;
    add(item("texture", name, texture_bytes(s.texture), true,
             dim(s.texture.width, s.texture.height) + (s.texture.mipmaps > 1 ? "  mips" : "")));
  }
  for (usize a = 0; a < ctx.texture.atlases.size(); a++) {
    const atlas_slot &atlas = ctx.texture.atlases[a];
    if (!atlas.alive)
      continue;
    for (usize p = 0; p < atlas.pages.size(); p++) {
      const Texture2D &t = atlas.pages[p].texture;
      add(item("texture", "atlas #" + std::to_string(a + 1) + " page " + std::to_string(p + 1),
               texture_bytes(t), true, dim(t.width, t.height)));
    }
  }
  for (usize i = 0; i < ctx.render_texture.slots.size(); i++) {
    const render_texture_slot &s = ctx.render_texture.slots[i];
    if (s.alive)
      add(item("target", "render texture #" + std::to_string(i + 1), target_bytes(s.target), true,
               dim(s.target.texture.width, s.target.texture.height)));
  }
  // One entry per typeface: its atlases, one for every pixel size drawn so far.
  const auto add_font = [&](const std::string &name, const font_slot &s) {
    if (!s.alive || s.atlases.empty())
      return;
    usize bytes = 0;
    std::string sizes;
    for (const auto &[px, font] : s.atlases) {
      bytes += texture_bytes(font.texture);
      sizes += (sizes.empty() ? "" : ", ") + std::to_string(px);
    }
    add(item("font", name, bytes, true,
             std::to_string(s.atlases.size()) + " atlas(es) at " + sizes + " px"));
  };
  add_font("font default", ctx.font.fallback);
  for (usize i = 0; i < ctx.font.slots.size(); i++)
    add_font("font #" + std::to_string(i + 1), ctx.font.slots[i]);
  for (usize i = 0; i < ctx.shader.slots.size(); i++) {
    const shader_slot &s = ctx.shader.slots[i];
    if (!s.alive)
      continue;
    std::string name = s.fs_path.empty() ? (s.vs_path.empty() ? "shader #" + std::to_string(i + 1) : s.vs_path) : s.fs_path;
    add(item("shader", name, 0, true, std::to_string(s.uniforms.size()) + " uniforms looked up"));
  }
  for (usize i = 0; i < ctx.audio.sounds.size(); i++) {
    const sound_slot &s = ctx.audio.sounds[i];
    if (!s.alive)
      continue;
    add(item("sound", "sound #" + std::to_string(i + 1), audio_bytes(s.sound.stream, s.sound.frameCount), false,
             std::to_string(s.voices.size() + 1) + " voices, bus " + std::to_string((i32)s.bus)));
  }
  for (usize i = 0; i < ctx.audio.musics.size(); i++) {
    const music_slot &s = ctx.audio.musics[i];
    if (s.alive)
      add(item("music", "music #" + std::to_string(i + 1), 0, false,
               "streamed, " + std::to_string(s.music.frameCount) + " frames"));
  }

  // Engine-owned buffers.
  usize chunks = 0, chunk_count = 0;
  for (const auto &[e, images] : ctx.sprites.chunks)
    for (const auto &[key, image] : images) {
      chunks += target_bytes(image.target);
      chunk_count++;
    }
  if (chunk_count > 0)
    add(item("engine", "tilemap chunk images", chunks, true, std::to_string(chunk_count) + " baked chunks"));
  if (IsRenderTextureValid(ctx.post.target))
    add(item("engine", "world target (post shader)", target_bytes(ctx.post.target), true,
             dim(ctx.post.target.texture.width, ctx.post.target.texture.height)));
  if (IsRenderTextureValid(ctx.view.target))
    add(item("engine", "virtual screen", target_bytes(ctx.view.target), true,
             dim(ctx.view.target.texture.width, ctx.view.target.texture.height)));
  const usize post = target_bytes(ctx.postfx.full_a) + target_bytes(ctx.postfx.full_b) +
                     target_bytes(ctx.postfx.half_a) + target_bytes(ctx.postfx.half_b);
  if (post > 0)
    add(item("engine", "post-processing buffers", post, true, "bloom and effect chain"));

  // Small engine tables, in RAM.
  usize tables = ctx.texture.slots.capacity() * sizeof(texture_slot) + ctx.audio.sounds.capacity() * sizeof(sound_slot) +
                 ctx.timers.timers.capacity() * sizeof(timer_job) + ctx.timers.tweens.capacity() * sizeof(tween_job);
  add(item("engine", "engine tables (slots, timers, tweens)", tables, false, ""));

  return json_value::make_object()
      .set("t", "res")
      .set("items", std::move(items))
      .set("gpu_bytes", (i64)gpu)
      .set("ram_bytes", (i64)ram);
}

entity_cost debug_entity_cost(const context &ctx, entt::entity entity) {
  const debug_state &d = ctx.debug;
  const entt::registry &reg = ctx.ecs.registry;
  entity_cost cost;
  for (auto [id, pool] : reg.storage()) {
    if (pool.info().hash() == entt::type_hash<entt::entity>::value() || !pool.contains(entity))
      continue;
    cost.ram += per_component_overhead;
    if (const auto it = d.types.find(pool.info().hash()); it != d.types.end()) {
      cost.ram += it->second.bytes;
      if (it->second.heap)
        cost.ram += it->second.heap(reg, entity);
    }
  }
  cost.gpu = debug_entity_gpu(ctx, entity);
  return cost;
}

usize debug_entity_gpu(const context &ctx, entt::entity entity) {
  const entt::registry &reg = ctx.ecs.registry;
  usize gpu = 0;
  if (reg.all_of<tilemap>(entity))
    gpu = chunk_bytes_of(ctx, entity);
  if (const particle_gpu_buffer *buffer = reg.try_get<particle_gpu_buffer>(entity))
    gpu += (usize)buffer->capacity;
  return gpu;
}

// Heap held by the engine's own component types, for debug.cpp to attach.
usize debug_heap_tilemap(const tilemap &m) {
  return m.chunks.size() * (sizeof(tile_chunk) + 32) + m.shapes.capacity() + m.anims.size() * sizeof(tile_anim);
}
usize debug_heap_particles(const particle_emitter &p) { return p.particles.capacity() * sizeof(particle); }
usize debug_heap_level_object(const level_object &o) {
  return o.name.capacity() + o.type.capacity() + o.points.capacity() * sizeof(vec2) + json_bytes(o.props);
}
} // namespace njin

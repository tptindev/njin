#include "debug.h"
#include "debug_prof.h"
#include "_comps.h"
#include "_tilemap.h"
#include "njin_anim.h"
#include "njin_body.h"
#include "njin_camera.h"
#include "njin_collision.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_fx.h"
#include "njin_level.h"
#include "njin_log.h"
#include "njin_log_impl.h"
#include "njin_particles.h"
#include "njin_version.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace njin {
namespace {
// The inspector speaks this version; it refuses a game with another one.
constexpr i32 protocol_version = 4;
// Log lines held while no inspector is connected, or between sends.
constexpr usize max_log_lines = 2000;

json_value vec(vec2 v) { return json_value::make_array().push(v.x).push(v.y); }
json_value color(rgba c) { return json_value::make_array().push(c.r).push(c.g).push(c.b).push(c.a); }
json_value rect_json(rect r) {
  return json_value::make_array().push(r.pos.x).push(r.pos.y).push(r.size.x).push(r.size.y);
}

u32 id_of(entt::entity e) { return (u32)entt::to_integral(e); }

// --- log forwarding ---

debug_state *tap_target = nullptr;

void log_tap(log_level level, const char *file, i32 line, const char *msg, void *) {
  if (tap_target == nullptr || !tap_target->running)
    return;
  const char *base = file;
  for (const char *c = file != nullptr ? file : ""; *c != '\0'; c++)
    if (*c == '/' || *c == '\\')
      base = c + 1;
  std::string src = base != nullptr ? base : "";
  if (line > 0)
    src += ":" + std::to_string(line);
  auto &lines = tap_target->log_lines;
  if (lines.size() >= max_log_lines)
    lines.erase(lines.begin());
  lines.push_back(json_dump(json_value::make_object()
                                .set("t", "log")
                                .set("lv", (i32)level)
                                .set("src", std::move(src))
                                .set("msg", msg != nullptr ? msg : ""),
                            false));
}

// --- built-in component views ---

void register_builtin(debug_state &d, entt::id_type type, const char *name, debug_component_fn fn,
                      std::size_t bytes) {
  d.types[type] = debug_type_entry{name, std::move(fn), bytes, nullptr};
}

template <class T, class Fn> void builtin(debug_state &d, const char *name, Fn fn) {
  register_builtin(d, entt::type_hash<T>::value(), name,
                   [fn](const entt::registry &reg, entt::entity e) { return fn(reg.get<T>(e)); }, sizeof(T));
}

// Says how much heap a builtin component owns, for the memory tables.
template <class T, class Fn> void builtin_heap(debug_state &d, Fn fn) {
  d.types[entt::type_hash<T>::value()].heap = [fn](const entt::registry &reg, entt::entity e) -> std::size_t {
    return fn(reg.get<T>(e));
  };
}

template <class T> void builtin_tag(debug_state &d, const char *name) {
  register_builtin(d, entt::type_hash<T>::value(), name,
                   [](const entt::registry &, entt::entity) { return json_value::make_object(); }, 0);
}

void register_builtins(njin_ctx &ctx) {
  debug_state &d = ctx.debug;
  builtin<transform>(d, "transform", [](const transform &t) {
    return json_value::make_object().set("pos", vec(t.pos)).set("rot", t.rot).set("scale", t.scale);
  });
  builtin<sprite>(d, "sprite", [](const sprite &s) {
    return json_value::make_object()
        .set("texture", s.texture.id)
        .set("source", rect_json(s.source))
        .set("origin", vec(s.origin))
        .set("tint", color(s.tint))
        .set("layer", s.layer)
        .set("flip_x", s.flip_x)
        .set("flip_y", s.flip_y)
        .set("visible", s.visible);
  });
  builtin<sprite_anim>(d, "sprite_anim", [](const sprite_anim &a) {
    return json_value::make_object()
        .set("first", a.first).set("count", a.count).set("frame", a.frame)
        .set("fps", a.fps).set("loop", a.loop).set("playing", a.playing).set("finished", a.finished);
  });
  builtin<collider>(d, "collider", [](const collider &c) {
    const char *shapes[] = {"box", "circle", "tiles"};
    return json_value::make_object()
        .set("shape", shapes[std::clamp((i32)c.shape, 0, 2)])
        .set("size", vec(c.size))
        .set("radius", c.radius)
        .set("offset", vec(c.offset))
        .set("layer", c.layer)
        .set("mask", c.mask)
        .set("trigger", c.trigger)
        .set("enabled", c.enabled);
  });
  builtin<child_of>(d, "child_of", [](const child_of &c) {
    return json_value::make_object()
        .set("parent", id_of(c.parent))
        .set("local_pos", vec(c.local.pos))
        .set("local_rot", c.local.rot)
        .set("local_scale", c.local.scale)
        .set("destroy_with_parent", c.destroy_with_parent);
  });
  builtin<camera_2d>(d, "camera_2d", [](const camera_2d &c) {
    return json_value::make_object().set("offset", vec(c.offset)).set("zoom", c.zoom);
  });
  builtin_tag<camera_on>(d, "camera_on");
  // Needs the scene names, so it captures the context.
  njin_ctx *pctx = &ctx;
  builtin<scene_owned>(d, "scene_owned", [pctx](const scene_owned &s) {
    const auto &scenes = pctx->scene.scenes;
    const char *name = s.scene.id >= 1 && s.scene.id <= scenes.size() ? scenes[s.scene.id - 1].name.c_str() : "?";
    return json_value::make_object().set("scene", name);
  });
  builtin<tilemap>(d, "tilemap", [](const tilemap &m) {
    return json_value::make_object()
        .set("tileset", m.tileset.id)
        .set("tile_size", vec(m.tile_size))
        .set("layer", m.layer)
        .set("visible", m.visible)
        .set("chunks", (i64)m.chunks.size());
  });
  // Not through builtin<>: it needs the entity, to find the emitter's GPU buffer.
  register_builtin(
      d, entt::type_hash<particle_emitter>::value(), "particle_emitter",
      [](const entt::registry &reg, entt::entity e) {
        const particle_emitter &p = reg.get<particle_emitter>(e);
        // On the GPU the vector still holds dead particles until the next cleanup.
        usize alive = p.particles.size();
        if (const auto *buffer = reg.try_get<particle_gpu_buffer>(e); buffer != nullptr && p.gpu)
          alive = particles_gpu_alive(p, *buffer);
        return json_value::make_object()
            .set("alive", (i64)alive)
            .set("gpu", p.gpu)
            .set("max", p.max_particles)
            .set("rate", p.rate)
            .set("emitting", p.emitting)
            .set("layer", p.layer)
            .set("visible", p.visible)
            .set("destroy_when_done", p.destroy_when_done);
      },
      sizeof(particle_emitter));
  builtin<level_object>(d, "level_object", [](const level_object &o) {
    return json_value::make_object()
        .set("name", o.name)
        .set("type", o.type)
        .set("size", vec(o.size))
        .set("points", (i64)o.points.size())
        .set("props", o.props);
  });
  builtin<flash_fx>(d, "flash_fx", [](const flash_fx &f) {
    return json_value::make_object().set("color", color(f.color)).set("duration", f.duration).set("time", f.time);
  });
  builtin<dissolve_fx>(d, "dissolve_fx", [](const dissolve_fx &f) {
    return json_value::make_object()
        .set("edge_color", color(f.edge_color))
        .set("duration", f.duration)
        .set("time", f.time)
        .set("reverse", f.reverse)
        .set("destroy_when_done", f.destroy_when_done);
  });
  builtin<animator>(d, "animator", [pctx](const animator &a) {
    return json_value::make_object()
        .set("current", animator_current(*pctx, a))
        .set("frame", a.frame)
        .set("speed", a.speed)
        .set("playing", a.playing)
        .set("finished", a.finished)
        .set("loops", a.loops);
  });
  builtin<platformer_body>(d, "platformer_body", [](const platformer_body &b) {
    return json_value::make_object()
        .set("velocity", vec(b.velocity)).set("grounded", b.grounded).set("on_slope", b.on_slope)
        .set("on_wall", b.on_wall).set("facing", b.facing)
        .set("run_speed", b.run_speed).set("jump_speed", b.jump_speed)
        .set("move_x", b.input.move_x).set("coyote", b.coyote_timer).set("jump_buffer", b.buffer_timer);
  });
  builtin<topdown_body>(d, "topdown_body", [](const topdown_body &b) {
    return json_value::make_object()
        .set("velocity", vec(b.velocity)).set("facing", vec(b.facing)).set("moving", b.moving)
        .set("dashing", b.dashing).set("speed", b.speed).set("dash_cooldown", b.cooldown_timer);
  });
  builtin<path_mover>(d, "path_mover", [](const path_mover &p) {
    return json_value::make_object()
        .set("points", (i64)p.points.size()).set("target", p.target).set("speed", p.speed)
        .set("loop", p.loop).set("paused", p.paused);
  });
  builtin<camera_follow>(d, "camera_follow", [](const camera_follow &f) {
    return json_value::make_object()
        .set("target", id_of(f.target)).set("offset", vec(f.offset)).set("deadzone", vec(f.deadzone))
        .set("smoothing", f.smoothing).set("lookahead", vec(f.lookahead)).set("look", vec(f.look));
  });
  builtin<platformer_input_map>(d, "platformer_input_map", [](const platformer_input_map &) {
    return json_value::make_object();
  });
  builtin<topdown_input_map>(d, "topdown_input_map", [](const topdown_input_map &) {
    return json_value::make_object();
  });
  builtin_tag<platform_rider>(d, "platform_rider");
  builtin_heap<path_mover>(d, [](const path_mover &p) -> std::size_t { return p.points.capacity() * sizeof(vec2); });
  builtin_heap<tilemap>(d, debug_heap_tilemap);
  builtin_heap<particle_emitter>(d, debug_heap_particles);
  builtin_heap<level_object>(d, debug_heap_level_object);
}

// Name for a component type: the registered one, else EnTT's type name
// without the njin:: prefix.
std::string type_name(const debug_state &d, const entt::type_info &info) {
  if (const auto it = d.types.find(info.hash()); it != d.types.end())
    return it->second.name;
  std::string n(info.name());
  // Compilers spell a type in an unnamed namespace differently; drop both,
  // and the engine's own prefix, so a game's `mover` reads as "mover".
  for (const char *prefix : {"njin::", "{anonymous}::", "(anonymous namespace)::", "`anonymous namespace'::"}) {
    const std::string p(prefix);
    if (n.rfind(p, 0) == 0)
      n.erase(0, p.size());
  }
  return n;
}

// A readable label for an entity: its level object's name or type, else the
// first component that is not one of the engine's own plumbing types.
std::string label_of(const entt::registry &reg, entt::entity e, const std::vector<std::string> &names) {
  if (const level_object *o = reg.try_get<level_object>(e)) {
    if (!o->name.empty() && o->name.size() < 40)
      return o->name;
    if (!o->type.empty())
      return o->type;
  }
  static const char *plumbing[] = {"transform", "scene_owned", "child_of", "sprite", "collider", "sprite_anim", "flash_fx", "dissolve_fx"};
  for (const std::string &n : names) {
    if (std::find(std::begin(plumbing), std::end(plumbing), n) == std::end(plumbing))
      return n;
  }
  return names.empty() ? std::string("entity") : names.front();
}

// --- commands from the inspector ---

entt::entity entity_from(const entt::registry &reg, const json_value &v) {
  const i64 raw = (i64)v.number_or(-1.0);
  if (raw < 0)
    return entt::null;
  const entt::entity e = (entt::entity)(u32)raw;
  return reg.valid(e) ? e : entt::null;
}

vec2 vec_from(const json_value &v, vec2 fallback) {
  return v.size() == 2 ? vec2{v[(usize)0].f32_or(fallback.x), v[(usize)1].f32_or(fallback.y)} : fallback;
}

void apply_set(entt::registry &reg, const json_value &cmd) {
  const entt::entity e = entity_from(reg, cmd["id"]);
  if (e == entt::null)
    return;
  const std::string comp = cmd["comp"].string_or("");
  const std::string field = cmd["field"].string_or("");
  const json_value &v = cmd["value"];
  if (comp == "transform") {
    // A child's transform is rewritten from its parent every frame: move its
    // local offset instead, or the edit would not stick.
    if (child_of *link = reg.try_get<child_of>(e)) {
      if (field == "pos") link->local.pos = vec_from(v, link->local.pos);
      if (field == "rot") link->local.rot = v.f32_or(link->local.rot);
      if (field == "scale") link->local.scale = v.f32_or(link->local.scale);
      return;
    }
    if (transform *t = reg.try_get<transform>(e)) {
      if (field == "pos") t->pos = vec_from(v, t->pos);
      if (field == "rot") t->rot = v.f32_or(t->rot);
      if (field == "scale") t->scale = v.f32_or(t->scale);
    }
  } else if (comp == "collider") {
    if (collider *c = reg.try_get<collider>(e)) {
      if (field == "enabled") c->enabled = v.bool_or(c->enabled);
      if (field == "trigger") c->trigger = v.bool_or(c->trigger);
    }
  } else if (comp == "sprite") {
    if (sprite *s = reg.try_get<sprite>(e)) {
      if (field == "visible") s->visible = v.bool_or(s->visible);
      if (field == "layer") s->layer = v.int_or(s->layer);
    }
  }
}

void apply(njin_ctx &ctx, const json_value &cmd) {
  debug_state &d = ctx.debug;
  entt::registry &reg = world(ctx);
  const std::string c = cmd["cmd"].string_or("");
  if (c == "pause") {
    ctx.time.paused = cmd["value"].bool_or(ctx.time.paused);
    d.step_pending = d.stepping = false;
  } else if (c == "step") {
    d.step_pending = true;
  } else if (c == "scale") {
    ctx.time.scale = std::max(0.0f, cmd["value"].f32_or(ctx.time.scale));
  } else if (c == "select") {
    d.selected = (i64)cmd["id"].number_or(-1.0);
    d.snapshot_timer = d.desc.snapshot_hz > 0.0f ? 1.0f / d.desc.snapshot_hz : 0.0f; // show it now
  } else if (c == "set") {
    apply_set(reg, cmd);
  } else if (c == "destroy") {
    if (const entt::entity e = entity_from(reg, cmd["id"]); e != entt::null)
      reg.destroy(e);
  } else if (c == "collision_debug") {
    collision_set_debug(ctx, cmd["value"].bool_or(false));
  } else if (c == "rec") {
    // Start or stop recording the game window to a GIF (see debug_recorder).
    if (cmd["value"].bool_or(false))
      debug_record_start(ctx, cmd["fps"].f32_or(15.0f), cmd["scale"].f32_or(0.5f), cmd["max_s"].f32_or(30.0f));
    else
      debug_record_stop(ctx);
    d.rec.dirty = true; // tell the inspector how it went, even when the start failed
  }
}

// --- snapshots ---

void send_hello(njin_ctx &ctx) {
  debug_state &d = ctx.debug;
  d.link.send(json_dump(json_value::make_object()
                            .set("t", "hello")
                            .set("v", protocol_version)
                            .set("title", ctx.cfg.title != nullptr ? ctx.cfg.title : "njin")
                            .set("fixed_hz", ctx.time.fixed_dt > 0.0f ? 1.0f / ctx.time.fixed_dt : 0.0f)
                            .set("pid", (i64)net_process_id())
                            .set("engine", version()),
                        false));
}

void send_stats(njin_ctx &ctx) {
  debug_state &d = ctx.debug;
  json_value frames = json_value::make_array();
  for (const f32 ms : d.frame_ms)
    frames.push(std::round(ms * 100.0f) / 100.0f);
  d.frame_ms.clear();
  const scene_handle s = ctx.scene.current;
  const char *scene = s.id >= 1 && s.id <= ctx.scene.scenes.size() ? ctx.scene.scenes[s.id - 1].name.c_str() : "";
  d.link.send(json_dump(json_value::make_object()
                            .set("t", "stats")
                            .set("frames", std::move(frames))
                            .set("entities", (i64)world(ctx).view<entt::entity>().size())
                            .set("paused", ctx.time.paused && !d.stepping)
                            .set("scale", ctx.time.scale)
                            .set("scene", scene)
                            .set("collision_debug", ctx.collision.debug)
                            .set("render", json_value::make_object()
                                               .set("sprites", (i64)ctx.stats.sprites)
                                               .set("sprites_culled", (i64)ctx.stats.sprites_culled)
                                               .set("tile_chunks", (i64)ctx.stats.tile_chunks)
                                               .set("emitters", (i64)ctx.stats.emitters)
                                               .set("emitters_culled", (i64)ctx.stats.emitters_culled)
                                               .set("particles", (i64)ctx.stats.particles)
                                               .set("particles_gpu", (i64)ctx.stats.particles_gpu)
                                               .set("instanced", (i64)ctx.stats.instanced_calls)
                                               .set("batches", (i64)ctx.stats.batches)
                                               .set("post_passes", (i64)ctx.stats.post_passes))
                            .set("dropped", (i64)d.link.dropped),
                        false));
}

void send_world(njin_ctx &ctx) {
  debug_state &d = ctx.debug;
  const entt::registry &reg = world(ctx);
  // Type table: each entity lists indexes into it instead of repeating names.
  std::vector<std::pair<const entt::basic_sparse_set<entt::entity> *, i32>> pools;
  json_value types = json_value::make_array();
  std::vector<std::string> type_names;
  for (auto [id, pool] : reg.storage()) {
    if (pool.info().hash() == entt::type_hash<entt::entity>::value() || pool.empty())
      continue;
    type_names.push_back(type_name(d, pool.info()));
    types.push(type_names.back());
    pools.push_back({&pool, (i32)type_names.size() - 1});
  }

  json_value ents = json_value::make_array();
  i32 count = 0;
  bool truncated = false;
  for (const entt::entity e : reg.view<entt::entity>()) {
    if (count >= d.desc.max_entities) {
      truncated = true;
      break;
    }
    count++;
    json_value comps = json_value::make_array();
    std::vector<std::string> names;
    for (const auto &[pool, index] : pools) {
      if (pool->contains(e)) {
        comps.push(index);
        names.push_back(type_names[(usize)index]);
      }
    }
    json_value row = json_value::make_object();
    row.set("id", id_of(e)).set("n", label_of(reg, e, names)).set("c", std::move(comps));
    if (const entity_cost cost = debug_entity_cost(ctx, e); cost.ram > 0) {
      row.set("m", (i64)cost.ram);
      if (cost.gpu > 0)
        row.set("g", (i64)cost.gpu);
    }
    if (const transform *t = reg.try_get<transform>(e))
      row.set("p", vec(t->pos));
    if (const collider *c = reg.try_get<collider>(e); c != nullptr && c->shape != collider_tiles) {
      const transform *t = reg.try_get<transform>(e);
      if (t != nullptr)
        row.set("col", json_value::make_object()
                           .set("s", c->shape == collider_circle ? 1 : 0)
                           .set("b", rect_json(collider_bounds(*t, *c)))
                           .set("tr", c->trigger)
                           .set("en", c->enabled));
    }
    ents.push(std::move(row));
  }

  // Occupied area of each tilemap, so the world view shows where the level is.
  json_value maps = json_value::make_array();
  for (auto [e, t, m] : reg.view<const transform, const tilemap>().each()) {
    if (m.chunks.empty())
      continue;
    bool first = true;
    vec2 lo{}, hi{};
    const vec2 size = m.tile_size * (f32)tile_chunk_size;
    for (const auto &[key, chunk] : m.chunks) {
      const cell c = tile_chunk_coord(key);
      const vec2 p0 = t.pos + vec2{(f32)c.x * size.x, (f32)c.y * size.y};
      const vec2 p1 = p0 + size;
      lo = first ? p0 : vec2{std::min(lo.x, p0.x), std::min(lo.y, p0.y)};
      hi = first ? p1 : vec2{std::max(hi.x, p1.x), std::max(hi.y, p1.y)};
      first = false;
    }
    const collider *col = reg.try_get<collider>(e);
    maps.push(json_value::make_object()
                  .set("id", id_of(e))
                  .set("b", rect_json(rect{lo, hi - lo}))
                  .set("solid", col != nullptr && col->shape == collider_tiles && col->enabled));
  }

  d.link.send(json_dump(json_value::make_object()
                            .set("t", "world")
                            .set("types", std::move(types))
                            .set("ents", std::move(ents))
                            .set("truncated", truncated)
                            .set("camera", rect_json(camera_bounds(ctx)))
                            .set("tilemaps", std::move(maps)),
                        false));
}

void send_entity(njin_ctx &ctx) {
  debug_state &d = ctx.debug;
  if (d.selected < 0)
    return;
  const entt::registry &reg = world(ctx);
  const entt::entity e = (entt::entity)(u32)d.selected;
  json_value msg = json_value::make_object();
  msg.set("t", "entity").set("id", d.selected).set("alive", reg.valid(e));
  json_value comps = json_value::make_array();
  if (reg.valid(e)) {
    for (auto [id, pool] : reg.storage()) {
      if (pool.info().hash() == entt::type_hash<entt::entity>::value() || !pool.contains(e))
        continue;
      json_value comp = json_value::make_object();
      comp.set("name", type_name(d, pool.info()));
      const auto it = d.types.find(pool.info().hash());
      comp.set("value", it != d.types.end() ? it->second.fn(reg, e) : json_value{});
      comps.push(std::move(comp));
    }
  }
  msg.set("comps", std::move(comps));
  d.link.send(json_dump(msg, false));
}

void send_watches(njin_ctx &ctx) {
  debug_state &d = ctx.debug;
  if (d.watches.empty())
    return;
  json_value values = json_value::make_object();
  for (const auto &[name, value] : d.watches)
    values.set(name, value);
  d.link.send(json_dump(json_value::make_object().set("t", "watch").set("values", std::move(values)), false));
}

// --- systems ---

void begin_frame(njin_ctx &ctx) {
  debug_state &d = ctx.debug;
  if (!d.running) {
    ctx.ecs.profile = false;
    return;
  }
  // Time every system while (and only while) an inspector is watching.
  ctx.ecs.profile = d.link.connected();
  // Undo the stepped frame: pause again before anything reads the clock.
  if (d.stepping) {
    d.stepping = false;
    ctx.time.paused = true;
    ctx.time.dt = 0.0f;
  }
  if (!d.link.connected()) {
    if (net_socket s = net_accept(d.listener); s.valid()) {
      d.link.sock = s;
      d.frame_ms.clear();
      send_hello(ctx);
      // The log is on the inspector's screen now (the lines so far are sent at the
      // end of this frame); the console stays quiet until it goes away.
      log_set_console(false);
      NJIN_INFO("debug: inspector connected");
    }
  }
  if (!d.link.connected())
    return;
  if (!d.link.pump()) {
    log_set_console(true);
    NJIN_INFO("debug: inspector disconnected");
    d.selected = -1;
    debug_record_stop(ctx); // nobody left to stop it: finish the file
    return;
  }
  std::string line;
  while (d.link.next(line)) {
    json_value cmd;
    if (json_parse(line, cmd))
      apply(ctx, cmd);
  }
  // A step asked while paused runs this one frame at a fixed length, then the
  // next frame pauses again (above).
  if (d.step_pending) {
    d.step_pending = false;
    if (ctx.time.paused) {
      ctx.time.paused = false;
      ctx.time.dt = ctx.time.fixed_dt;
      d.stepping = true;
    }
  }
}

void end_frame(njin_ctx &ctx) {
  debug_state &d = ctx.debug;
  if (!d.running || !d.link.connected())
    return;
  d.frame_ms.push_back(ctx.time.dt_real * 1000.0f);
  for (const std::string &l : d.log_lines)
    d.link.send(l);
  d.log_lines.clear();
  d.snapshot_timer += ctx.time.dt_real;
  const f32 period = d.desc.snapshot_hz > 0.0f ? 1.0f / d.desc.snapshot_hz : 0.1f;
  if (d.snapshot_timer >= period) {
    d.snapshot_timer = 0.0f;
    send_stats(ctx);
    send_world(ctx);
    send_entity(ctx);
    send_watches(ctx);
    d.link.send(json_dump(debug_build_prof(ctx), false));
  }
  // The recording: progress while it runs, and once more whenever its state changed.
  if (d.rec.active || d.rec.dirty) {
    d.rec.dirty = false;
    d.link.send(json_dump(debug_record_status(ctx), false));
  }
  // The memory and asset tables walk every component and resource: once a second.
  d.slow_timer += ctx.time.dt_real;
  if (d.slow_timer >= 1.0f) {
    d.slow_timer = 0.0f;
    d.link.send(json_dump(debug_build_mem(ctx), false));
    d.link.send(json_dump(debug_build_res(ctx), false));
  }
  if (!d.link.pump())
    d.selected = -1;
}

void setup(njin_ctx &ctx) {
  ecs_register(ctx, phase_pre_update, begin_frame, "begin_frame");
  ecs_register(ctx, phase_post_update, end_frame, "end_frame");
}
} // namespace

render_info render_info_get(const njin_ctx &ctx) {
  const render_stats &s = ctx.stats;
  return render_info{.sprites = s.sprites,
                     .sprites_culled = s.sprites_culled,
                     .tile_chunks = s.tile_chunks,
                     .emitters = s.emitters,
                     .emitters_culled = s.emitters_culled,
                     .particles = s.particles,
                     .particles_gpu = s.particles_gpu,
                     .instanced_calls = s.instanced_calls,
                     .draw_calls = s.batches,
                     .post_passes = s.post_passes};
}

mod_desc debug_module() { return mod_desc{.name = "njin.debug", .setup = setup}; }

bool debug_server_start(njin_ctx &ctx, const debug_server_desc &desc) {
  debug_state &d = ctx.debug;
  if (d.running)
    debug_server_stop(ctx);
  d.listener = net_listen(desc.port);
  if (!d.listener.valid()) {
    log_hold(false); // nobody can attach: stop keeping lines for it
    NJIN_WARN("debug: cannot listen on 127.0.0.1:%u (port in use?)", (unsigned)desc.port);
    return false;
  }
  d.desc = desc;
  d.running = true;
  if (d.types.empty())
    register_builtins(ctx);
  tap_target = &d;
  log_set_tap(log_tap, nullptr);
  NJIN_INFO("debug: waiting for njin_inspector on 127.0.0.1:%u", (unsigned)desc.port);
  return true;
}

debug_state::~debug_state() {
  if (tap_target == this) {
    log_set_tap(nullptr, nullptr);
    log_set_console(true);
    tap_target = nullptr;
  }
}

void debug_server_stop(njin_ctx &ctx) {
  debug_state &d = ctx.debug;
  if (tap_target == &d) {
    log_set_tap(nullptr, nullptr);
    log_set_console(true);
    tap_target = nullptr;
  }
  debug_record_stop(ctx);
  d.link.close();
  net_close(d.listener);
  d.running = false;
  d.log_lines.clear();
  d.selected = -1;
}

bool debug_server_connected(const njin_ctx &ctx) { return ctx.debug.link.connected(); }

void debug_watch(njin_ctx &ctx, const char *name, json_value value) {
  debug_state &d = ctx.debug;
  if (!d.running || name == nullptr)
    return;
  for (auto &[n, v] : d.watches) {
    if (n == name) {
      v = std::move(value);
      return;
    }
  }
  d.watches.emplace_back(name, std::move(value));
}

void debug_watch(njin_ctx &ctx, const char *name, vec2 value) { debug_watch(ctx, name, vec(value)); }

void debug_component(njin_ctx &ctx, entt::id_type type, const char *name, debug_component_fn fn,
                     std::size_t bytes) {
  if (name == nullptr || !fn)
    return;
  ctx.debug.types[type] = debug_type_entry{name, std::move(fn), bytes, nullptr};
}
} // namespace njin

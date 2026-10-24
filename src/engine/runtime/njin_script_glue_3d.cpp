// The `njin` Lua module's 3D part, glue between the script and 3d features
// (njin_features.h): 3D models and drawing, 3D lights, sky and weather, 3D
// screen effects and decals. Thin bindings over the public API.
#include "njin_script_rt.h"

#include "_collide.h"
#include "_comps.h"
#include "_math.h"
#include "_tilemap.h"
#include "njin_cfg.h"
#include "njin_audio.h"
#include "njin_ctx.h"
#include "njin_fx.h"
#include "njin_3d.h"
#include "njin_post3d.h"
#include "njin_world3d.h"
#include "njin_ctx_impl.h"
#include "njin_model.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <span>

namespace njin {
namespace {
// Raises a Lua error at the calling script's file:line (luaL_error adds it);
// the script's error handler logs it and the game keeps running.
[[noreturn]] void fail(lua_State *L, const char *fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, args);
  va_end(args);
  luaL_error(L, "%s", buf);
  for (;;) {
  }
}

f32 num(const sol::table &t, const char *key, f32 fallback) { return t[key].get_or(fallback); }
bool flag(const sol::table &t, const char *key, bool fallback) { return t[key].get_or(fallback); }

// A vec3 usertype, or a table {x, y, z} / {1, 2, 3}.
vec3 vec3_of(const sol::object &o, vec3 fallback) {
  if (o.is<vec3>())
    return o.as<vec3>();
  if (o.get_type() == sol::type::table) {
    const sol::table t = o.as<sol::table>();
    return {t["x"].get_or(t[1].get_or(fallback.x)), t["y"].get_or(t[2].get_or(fallback.y)),
            t["z"].get_or(t[3].get_or(fallback.z))};
  }
  return fallback;
}

vec2 vec2_of(const sol::object &o, vec2 fallback) {
  if (o.is<vec2>())
    return o.as<vec2>();
  if (o.get_type() == sol::type::table) {
    const sol::table t = o.as<sol::table>();
    return {t["x"].get_or(t[1].get_or(fallback.x)), t["y"].get_or(t[2].get_or(fallback.y))};
  }
  return fallback;
}

// A colour table {r, g, b, a} or {1, 0, 0, 1}; nil is `fallback`.
rgba color_of(const sol::object &o, rgba fallback = colors::white) {
  if (o.get_type() != sol::type::table)
    return fallback;
  const sol::table t = o.as<sol::table>();
  return {t["r"].get_or(t[1].get_or(fallback.r)), t["g"].get_or(t[2].get_or(fallback.g)),
          t["b"].get_or(t[3].get_or(fallback.b)), t["a"].get_or(t[4].get_or(fallback.a))};
}

transform3d transform_of(const sol::object &o) {
  transform3d t{};
  if (o.is<vec3>()) {
    t.position = o.as<vec3>();
    return t;
  }
  if (o.get_type() != sol::type::table)
    return t;
  const sol::table tb = o.as<sol::table>();
  t.position = vec3_of(tb["position"], t.position);
  t.rotation = vec3_of(tb["rotation"], t.rotation);
  const sol::object s = tb["scale"];
  if (s.get_type() == sol::type::number) {
    const f32 k = s.as<f32>();
    t.scale = {k, k, k};
  } else {
    t.scale = vec3_of(s, t.scale);
  }
  return t;
}

camera3d camera_of(const sol::table &t) {
  camera3d c{};
  c.position = vec3_of(t["position"], c.position);
  c.target = vec3_of(t["target"], c.target);
  c.up = vec3_of(t["up"], c.up);
  c.fovy = num(t, "fovy", c.fovy);
  c.near_plane = num(t, "near", c.near_plane);
  c.far_plane = num(t, "far", c.far_plane);
  c.entities = flag(t, "entities", c.entities);
  return c;
}

model_handle model_checked(context &ctx, lua_State *L, lua_Integer id, const char *fn) {
  const model_handle h{(u32)id};
  if (id <= 0 || model_slot_of(ctx.model, h) == nullptr)
    fail(L, "njin.%s: %lld is not a loaded model", fn, (long long)id);
  return h;
}

// An animation given by name or 0-based index; nil is -1 (the rest pose).
i32 anim_of(const context &ctx, model_handle m, const sol::object &o) {
  if (o.get_type() == sol::type::number)
    return o.as<i32>();
  if (o.get_type() == sol::type::string)
    return model_anim_find(ctx, m, o.as<std::string>().c_str());
  return -1;
}

// model_pose from a table: anim, time, loop, blend_anim, blend_time, blend_loop,
// blend. Morph weights by name go to `weights` (kept alive by the caller).
model_pose pose_of(const context &ctx, model_handle m, const sol::table &t, std::vector<f32> &weights) {
  model_pose p{};
  p.anim = anim_of(ctx, m, t["anim"]);
  p.time = num(t, "time", 0.0f);
  p.loop = flag(t, "loop", true);
  p.blend_anim = anim_of(ctx, m, t["blend_anim"]);
  p.blend_time = num(t, "blend_time", p.time);
  p.blend_loop = flag(t, "blend_loop", true);
  p.blend = num(t, "blend", 0.0f);
  const sol::object morphs = t["morphs"];
  if (morphs.get_type() == sol::type::table) {
    weights.assign((usize)std::max(0, model_morph_count(ctx, m)), 0.0f);
    for (const auto &[k, v] : morphs.as<sol::table>()) {
      if (k.get_type() != sol::type::string || v.get_type() != sol::type::number)
        continue;
      const i32 i = model_morph_find(ctx, m, k.as<std::string>().c_str());
      if (i >= 0 && i < (i32)weights.size())
        weights[(usize)i] = v.as<f32>();
    }
    p.morph_weights = weights.data();
    p.morph_count = (i32)weights.size();
  }
  return p;
}

// A point from model space to the world, as the engine places models
// (scale, then rotate z, x, y, then translate).
vec3 to_world(const transform3d &t, vec3 p) {
  constexpr f32 rad = 3.14159265358979f / 180.0f;
  p = {p.x * t.scale.x, p.y * t.scale.y, p.z * t.scale.z};
  const f32 cz = std::cos(t.rotation.z * rad), sz = std::sin(t.rotation.z * rad);
  p = {p.x * cz - p.y * sz, p.x * sz + p.y * cz, p.z};
  const f32 cx = std::cos(t.rotation.x * rad), sx = std::sin(t.rotation.x * rad);
  p = {p.x, p.y * cx - p.z * sx, p.y * sx + p.z * cx};
  const f32 cy = std::cos(t.rotation.y * rad), sy = std::sin(t.rotation.y * rad);
  p = {p.x * cy + p.z * sy, p.y, -p.x * sy + p.z * cy};
  return p + t.position;
}

void apply_material(material3d &m, const sol::table &t) {
  m.specular = num(t, "specular", m.specular);
  m.shininess = num(t, "shininess", m.shininess);
  m.emission = color_of(t["emission"], m.emission);
  m.rim = color_of(t["rim"], m.rim);
  m.unlit = flag(t, "unlit", m.unlit);
  m.cast_shadows = flag(t, "cast_shadows", m.cast_shadows);
  m.reflect = num(t, "reflect", m.reflect);
  m.world_uv = num(t, "world_uv", m.world_uv);
}

void apply_light3d(light3d &l, const sol::table &t) {
  l.direction = vec3_of(t["direction"], l.direction);
  l.color = color_of(t["color"], l.color);
  l.ambient = color_of(t["ambient"], l.ambient);
  l.shadows = flag(t, "shadows", l.shadows);
  l.shadow_range = num(t, "shadow_range", l.shadow_range);
  l.shadow_softness = num(t, "shadow_softness", l.shadow_softness);
  l.fog_color = color_of(t["fog_color"], l.fog_color);
  l.fog_density = num(t, "fog_density", l.fog_density);
}

sol::table light3d_table(sol::state_view lua, const light3d &l) {
  const auto col = [&](rgba c) { return lua.create_table_with("r", c.r, "g", c.g, "b", c.b, "a", c.a); };
  return lua.create_table_with("direction", l.direction, "color", col(l.color), "ambient", col(l.ambient), "shadows",
                               l.shadows, "shadow_range", l.shadow_range, "fog_color", col(l.fog_color),
                               "fog_density", l.fog_density);
}

bool weather_kind_of(const std::string &name, weather3d_kind &out) {
  static constexpr std::pair<const char *, weather3d_kind> kinds[] = {{"clear", weather3d_clear},
                                                                       {"overcast", weather3d_overcast},
                                                                       {"rain", weather3d_rain},
                                                                       {"snow", weather3d_snow},
                                                                       {"fog", weather3d_fog}};
  for (const auto &[n, k] : kinds)
    if (name == n) {
      out = k;
      return true;
    }
  return false;
}

// A weather: a preset name, or a table (optionally with `preset`) of fields.
weather3d weather_of(lua_State *L, const sol::object &o) {
  weather3d w{};
  if (o.get_type() == sol::type::string) {
    weather3d_kind k{};
    if (!weather_kind_of(o.as<std::string>(), k))
      fail(L, "njin: unknown weather '%s' (clear, overcast, rain, snow, fog)", o.as<std::string>().c_str());
    return weather3d_preset(k);
  }
  if (o.get_type() != sol::type::table)
    return w;
  const sol::table t = o.as<sol::table>();
  if (sol::optional<std::string> p = t["preset"])
    w = weather_of(L, sol::make_object(L, *p));
  w.clouds = num(t, "clouds", w.clouds);
  w.cloud_darkness = num(t, "cloud_darkness", w.cloud_darkness);
  w.fog = num(t, "fog", w.fog);
  w.rain = num(t, "rain", w.rain);
  w.snow = num(t, "snow", w.snow);
  w.wind = vec2_of(t["wind"], w.wind);
  w.wetness = num(t, "wetness", w.wetness);
  w.cover_auto = flag(t, "cover_auto", w.cover_auto);
  return w;
}

sky3d sky_of(lua_State *L, const sol::object &o) {
  sky3d s{};
  if (o.get_type() != sol::type::table)
    return s;
  const sol::table t = o.as<sol::table>();
  s.hour = num(t, "hour", s.hour);
  s.latitude = num(t, "latitude", s.latitude);
  s.season = num(t, "season", s.season);
  s.north = num(t, "north", s.north);
  s.weather = weather_of(L, t["weather"]);
  s.cloud_scale = num(t, "cloud_scale", s.cloud_scale);
  s.stars = flag(t, "stars", s.stars);
  s.sun_size = num(t, "sun_size", s.sun_size);
  s.drive_light = flag(t, "drive_light", s.drive_light);
  return s;
}

void script_bind_3d(context &ctx, script_runtime &rt) {
  sol::table n = rt.lua["njin"];
  context *c = &ctx;
  script_runtime *r = &rt;

  // --- 3D sound
  n["sound_play3d"] = sol::overload(
      [c](lua_Integer id, vec3 at) { sound_play3d(*c, sound_handle{.id = (u32)id}, at); },
      [c](lua_Integer id, lua_Integer entity) { sound_play3d(*c, sound_handle{.id = (u32)id}, script_entity(entity)); });

  // --- 3D drawing (on_render: begin_3d ... end_3d)
  n["begin_3d"] = [c](sol::table camera) { begin_3d(*c, camera_of(camera)); };
  n["end_3d"] = [c] { end_3d(*c); };
  n["model_load"] = [c, r](const std::string &path) -> sol::optional<lua_Integer> {
    const auto it = r->models.find(path);
    if (it != r->models.end())
      return (lua_Integer)it->second;
    const model_handle h = model_load(*c, path.c_str());
    if (h.id == 0)
      return sol::nullopt;
    r->models[path] = h.id;
    return (lua_Integer)h.id;
  };
  n["model_valid"] = [c](lua_Integer id) { return id > 0 && model_slot_of(c->model, model_handle{(u32)id}) != nullptr; };
  n["model_anim_count"] = [c](sol::this_state s, lua_Integer m) {
    return model_anim_count(*c, model_checked(*c, s, m, "model_anim_count"));
  };
  n["model_anim_find"] = [c](sol::this_state s, lua_Integer m, const std::string &name) -> sol::optional<i32> {
    const i32 i = model_anim_find(*c, model_checked(*c, s, m, "model_anim_find"), name.c_str());
    return i < 0 ? sol::nullopt : sol::optional<i32>(i);
  };
  n["model_anim_name"] = [c](sol::this_state s, lua_Integer m, i32 index) {
    return std::string(model_anim_name(*c, model_checked(*c, s, m, "model_anim_name"), index));
  };
  n["model_anim_duration"] = [c](sol::this_state s, lua_Integer m, sol::object anim) {
    const model_handle h = model_checked(*c, s, m, "model_anim_duration");
    return model_anim_duration(*c, h, anim_of(*c, h, anim));
  };
  n["model_morph_count"] = [c](sol::this_state s, lua_Integer m) {
    return model_morph_count(*c, model_checked(*c, s, m, "model_morph_count"));
  };
  n["model_bone_count"] = [c](sol::this_state s, lua_Integer m) {
    return model_bone_count(*c, model_checked(*c, s, m, "model_bone_count"));
  };
  n["model_bone_find"] = [c](sol::this_state s, lua_Integer m, const std::string &name) -> sol::optional<i32> {
    const i32 i = model_bone_find(*c, model_checked(*c, s, m, "model_bone_find"), name.c_str());
    return i < 0 ? sol::nullopt : sol::optional<i32>(i);
  };
  // World position of a bone for a draw at `transform` in `pose` (to attach things).
  n["model_bone_position"] = [c](sol::this_state s, lua_Integer m, sol::object bone, sol::object transform,
                                 sol::optional<sol::table> pose) -> sol::optional<vec3> {
    const model_handle h = model_checked(*c, s, m, "model_bone_position");
    const i32 b = bone.get_type() == sol::type::string ? model_bone_find(*c, h, bone.as<std::string>().c_str())
                                                       : bone.as<i32>();
    if (b < 0 || b >= model_bone_count(*c, h))
      return sol::nullopt;
    std::vector<f32> weights;
    const model_pose p = pose ? pose_of(*c, h, *pose, weights) : model_pose{};
    return to_world(transform_of(transform), model_bone_pose(*c, h, p, b).position);
  };
  // draw_model(m, transform, {anim=, time=, blend_anim=, blend=, morphs={name=w}, tint=})
  n["draw_model"] = [c](sol::this_state s, lua_Integer m, sol::object transform, sol::optional<sol::table> opts) {
    const model_handle h = model_checked(*c, s, m, "draw_model");
    const transform3d t = transform_of(transform);
    if (!opts) {
      draw_model(*c, h, t);
      return;
    }
    std::vector<f32> weights;
    const model_pose p = pose_of(*c, h, *opts, weights);
    draw_model_anim(*c, h, t, p, color_of((*opts)["tint"]));
  };
  n["draw_cube3d"] = [c](sol::object center, sol::object size, sol::object color) {
    draw_cube3d(*c, vec3_of(center, {}), vec3_of(size, {1, 1, 1}), color_of(color));
  };
  n["draw_sphere3d"] = [c](sol::object center, f32 radius, sol::object color) {
    draw_sphere3d(*c, vec3_of(center, {}), radius, color_of(color));
  };
  n["draw_cylinder3d"] = [c](sol::object from, sol::object to, f32 radius, sol::object color) {
    draw_cylinder3d(*c, vec3_of(from, {}), vec3_of(to, {0, 1, 0}), radius, color_of(color));
  };
  n["draw_capsule3d"] = [c](sol::object from, sol::object to, f32 radius, sol::object color) {
    draw_capsule3d(*c, vec3_of(from, {}), vec3_of(to, {0, 1, 0}), radius, color_of(color));
  };
  n["draw_plane3d"] = [c](sol::object center, sol::object size, sol::object color) {
    draw_plane3d(*c, vec3_of(center, {}), vec2_of(size, {1, 1}), color_of(color));
  };
  // draw_shape3d({kind="sphere"|"box"|"capsule"|"cylinder"|"torus", position=, rotation=, size=,
  //               radius=, height=, thickness=, rounding=}, color)
  n["draw_shape3d"] = [c](sol::this_state s, sol::table t, sol::object color) {
    shape3d sh{};
    const std::string kind = t["kind"].get_or(std::string("sphere"));
    if (kind == "sphere")
      sh.kind = shape3d_sphere;
    else if (kind == "box")
      sh.kind = shape3d_box;
    else if (kind == "capsule")
      sh.kind = shape3d_capsule;
    else if (kind == "cylinder")
      sh.kind = shape3d_cylinder;
    else if (kind == "torus")
      sh.kind = shape3d_torus;
    else
      fail(s, "njin.draw_shape3d: unknown kind '%s'", kind.c_str());
    sh.position = vec3_of(t["position"], sh.position);
    sh.rotation = vec3_of(t["rotation"], sh.rotation);
    sh.size = vec3_of(t["size"], sh.size);
    sh.radius = num(t, "radius", sh.radius);
    sh.height = num(t, "height", sh.height);
    sh.thickness = num(t, "thickness", sh.thickness);
    sh.rounding = num(t, "rounding", sh.rounding);
    draw_shape3d(*c, sh, color_of(color));
  };
  n["material3d_set"] = [c](sol::optional<sol::table> t) {
    material3d m{};
    if (t)
      apply_material(m, *t);
    material3d_set(*c, m);
  };
  // The model3d component: an entity drawn in every 3D pass at its transform3d.
  n["model3d_set"] = [c](sol::this_state s, lua_Integer entity, sol::table t) {
    entt::registry &w = world(*c);
    const entt::entity e = script_entity(entity);
    if (!w.valid(e))
      fail(s, "njin.model3d_set: %lld is not an entity", (long long)entity);
    model3d &m = w.get_or_emplace<model3d>(e);
    if (sol::optional<lua_Integer> id = t["model"])
      m.model = model_checked(*c, s, *id, "model3d_set");
    if (t["anim"].valid())
      m.pose.anim = anim_of(*c, m.model, t["anim"]);
    m.pose.time = num(t, "time", m.pose.time);
    m.pose.loop = flag(t, "loop", m.pose.loop);
    m.speed = num(t, "speed", m.speed);
    m.tint = color_of(t["tint"], m.tint);
    m.visible = flag(t, "visible", m.visible);
    static_cast<void>(w.get_or_emplace<transform3d>(e));
  };
  n["model3d"] = [c, r](lua_Integer entity) -> sol::object {
    entt::registry &w = world(*c);
    const entt::entity e = script_entity(entity);
    const model3d *m = w.valid(e) ? w.try_get<model3d>(e) : nullptr;
    if (m == nullptr)
      return sol::make_object(r->lua, sol::lua_nil);
    return r->lua.create_table_with("model", (lua_Integer)m->model.id, "anim", m->pose.anim, "time", m->pose.time,
                                    "loop", m->pose.loop, "speed", m->speed, "visible", m->visible);
  };

  // --- 3D lights and sky
  n["light3d_set"] = [c](sol::table t) {
    light3d l = light3d_get(*c);
    apply_light3d(l, t);
    light3d_set(*c, l);
  };
  n["light3d_get"] = [c, r] { return light3d_table(r->lua, light3d_get(*c)); };
  // light3d_add({kind="point"|"spot", position=, direction=, color=, intensity=, radius=, cone=,
  //              softness=, shadows=}) for the open 3D pass.
  n["light3d_add"] = [c](sol::table t) {
    light3d_source l{};
    l.kind = t["kind"].get_or(std::string("point")) == "spot" ? light3d_spot : light3d_point;
    l.position = vec3_of(t["position"], l.position);
    l.direction = vec3_of(t["direction"], l.direction);
    l.color = color_of(t["color"], l.color);
    l.intensity = num(t, "intensity", l.intensity);
    l.radius = num(t, "radius", l.radius);
    l.cone = num(t, "cone", l.cone);
    l.softness = num(t, "softness", l.softness);
    l.shadows = flag(t, "shadows", l.shadows);
    light3d_add(*c, l);
  };
  n["draw_sky3d"] = [c](sol::this_state s, sol::object sky) { draw_sky3d(*c, sky_of(s, sky)); };
  n["sky3d_sun_direction"] = [](sol::this_state s, sol::object sky) { return sky3d_sun_direction(sky_of(s, sky)); };
  n["weather3d_preset"] = [r](sol::this_state s, const std::string &name) {
    const weather3d w = weather_of(s, sol::make_object(s, name));
    return r->lua.create_table_with("clouds", w.clouds, "cloud_darkness", w.cloud_darkness, "fog", w.fog, "rain",
                                    w.rain, "snow", w.snow, "wind", w.wind, "wetness", w.wetness, "cover_auto",
                                    w.cover_auto);
  };

  // --- 3D screen effects and decals
  n["post3d_set"] = [c](sol::table t) {
    post3d p = post3d_get(*c);
    p.ssao = num(t, "ssao", p.ssao);
    p.ssao_radius = num(t, "ssao_radius", p.ssao_radius);
    p.ssao_half = flag(t, "ssao_half", p.ssao_half);
    p.ssr = num(t, "ssr", p.ssr);
    p.motion_blur = num(t, "motion_blur", p.motion_blur);
    p.shafts = num(t, "shafts", p.shafts);
    p.flare = num(t, "flare", p.flare);
    p.taa = flag(t, "taa", p.taa);
    p.taa_sharpen = num(t, "taa_sharpen", p.taa_sharpen);
    post3d_set(*c, p);
  };
  n["post3d_off"] = [c] { post3d_set(*c, post3d{}); };
  // decal3d_add({position=, normal= (or rotation=), size=, color=, lifetime=, fade=, paint=})
  n["decal3d_add"] = [c](sol::table t) -> sol::optional<lua_Integer> {
    decal3d_desc d{};
    d.position = vec3_of(t["position"], d.position);
    if (t["normal"].valid())
      d.rotation = decal3d_rotation(vec3_of(t["normal"], {0, 1, 0}));
    else
      d.rotation = vec3_of(t["rotation"], d.rotation);
    d.size = vec3_of(t["size"], d.size);
    d.color = color_of(t["color"], d.color);
    d.blend = flag(t, "paint", false) ? decal3d_paint : decal3d_multiply;
    d.lifetime = num(t, "lifetime", d.lifetime);
    d.fade = num(t, "fade", d.fade);
    const decal3d_handle h = decal3d_add(*c, d);
    return h.id == 0 ? sol::nullopt : sol::optional<lua_Integer>((lua_Integer)h.id);
  };
  n["decal3d_remove"] = [c](lua_Integer id) { decal3d_remove(*c, decal3d_handle{(u32)id}); };
  n["decal3d_clear"] = [c] { decal3d_clear(*c); };
}
} // namespace

void feature_install_script_3d(context &ctx) { ctx.script->binders.push_back(script_bind_3d); }
} // namespace njin

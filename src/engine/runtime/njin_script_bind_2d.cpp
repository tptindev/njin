// The `njin` Lua module, 2D gameplay: bodies, collision, sprites and their
// animation, the 2D camera, tilemaps and particles. Thin bindings over the
// public API, split from njin_script_bind.cpp so the parts compile in parallel.
#include "njin_script_rt.h"

#include "_collide.h"
#include "_comps.h"
#include "_math.h"
#include "_tilemap.h"
#include "njin_cfg.h"
#include "njin_collision.h"
#include "njin_anim.h"
#include "njin_body.h"
#include "njin_camera.h"
#include "njin_ctx.h"
#include "njin_json.h"
#include "njin_particles.h"

namespace njin {
void script_bind_njin_2d(context &ctx, script_runtime &rt, sol::table n) {
  context *c = &ctx;
  script_runtime *r = &rt;
  const auto reg = [c]() -> entt::registry & { return world(*c); };
  const auto ent = [](lua_Integer v) { return script_entity(v); };
  const auto lua_id = [](entt::entity e) -> sol::optional<lua_Integer> {
    if (e == entt::null)
      return sol::nullopt;
    return (lua_Integer)entt::to_integral(e);
  };


  // --- 2D bodies
  n["platformer_input"] = [reg, ent](lua_Integer id, f32 move_x, sol::optional<bool> jump,
                                     sol::optional<bool> jump_held, sol::optional<bool> drop) {
    platformer_body *b = reg().valid(ent(id)) ? reg().try_get<platformer_body>(ent(id)) : nullptr;
    if (b == nullptr)
      return;
    b->input.move_x = move_x;
    b->input.jump = b->input.jump || jump.value_or(false); // a press waits for the next fixed step
    b->input.jump_held = jump_held.value_or(jump.value_or(false));
    b->input.drop = b->input.drop || drop.value_or(false);
  };
  n["platformer"] = [reg, ent, r](lua_Integer id) -> sol::object {
    const platformer_body *b = reg().valid(ent(id)) ? reg().try_get<platformer_body>(ent(id)) : nullptr;
    if (b == nullptr)
      return sol::make_object(r->lua, sol::lua_nil);
    return r->lua.create_table_with("velocity", b->velocity, "grounded", b->grounded, "on_slope", b->on_slope,
                                    "on_wall", b->on_wall, "facing", b->facing, "jumped", b->jumped, "landed",
                                    b->landed);
  };
  n["platformer_set_velocity"] = [reg, ent](lua_Integer id, vec2 v) {
    if (platformer_body *b = reg().valid(ent(id)) ? reg().try_get<platformer_body>(ent(id)) : nullptr)
      b->velocity = v;
  };
  n["topdown_input"] = [reg, ent](lua_Integer id, vec2 move, sol::optional<bool> dash) {
    topdown_body *b = reg().valid(ent(id)) ? reg().try_get<topdown_body>(ent(id)) : nullptr;
    if (b == nullptr)
      return;
    b->input.move = move;
    b->input.dash = b->input.dash || dash.value_or(false);
  };
  n["topdown"] = [reg, ent, r](lua_Integer id) -> sol::object {
    const topdown_body *b = reg().valid(ent(id)) ? reg().try_get<topdown_body>(ent(id)) : nullptr;
    if (b == nullptr)
      return sol::make_object(r->lua, sol::lua_nil);
    return r->lua.create_table_with("velocity", b->velocity, "facing", b->facing, "moving", b->moving, "dashing",
                                    b->dashing);
  };
  n["topdown_set_velocity"] = [reg, ent](lua_Integer id, vec2 v) {
    if (topdown_body *b = reg().valid(ent(id)) ? reg().try_get<topdown_body>(ent(id)) : nullptr)
      b->velocity = v;
  };

  // --- 2D collision
  n["collision_move"] = [c, reg, ent, r, lua_id](lua_Integer id, vec2 d) -> sol::object {
    if (!reg().valid(ent(id)))
      return sol::make_object(r->lua, sol::lua_nil);
    const collision_move_result m = collision_move(*c, ent(id), d);
    sol::table t = r->lua.create_table_with("moved", m.moved, "hit_x", m.hit_x, "hit_y", m.hit_y, "grounded",
                                            m.grounded, "on_slope", m.on_slope);
    t["other_x"] = lua_id(m.other_x);
    t["other_y"] = lua_id(m.other_y);
    return t;
  };
  const auto entity_list = [r, lua_id](const std::vector<entt::entity> &list) {
    sol::table t = r->lua.create_table((int)list.size(), 0);
    for (usize i = 0; i < list.size(); i++)
      t[i + 1] = lua_id(list[i]);
    return t;
  };
  n["overlap_rect"] = [c, entity_list](f32 x, f32 y, f32 w, f32 h) {
    std::vector<entt::entity> out;
    collision_overlap_rect(*c, rect{{x, y}, {w, h}}, &out);
    return entity_list(out);
  };
  n["overlap_point"] = [c, entity_list](vec2 p) {
    std::vector<entt::entity> out;
    collision_overlap_point(*c, p, &out);
    return entity_list(out);
  };
  n["raycast2d"] = [c, r, lua_id, ent](vec2 from, vec2 to, sol::optional<lua_Integer> ignore) -> sol::object {
    const raycast_hit h =
        collision_raycast(*c, from, to, layer_all, false, ignore ? ent(*ignore) : entt::entity(entt::null));
    if (!h.hit)
      return sol::make_object(r->lua, sol::lua_nil);
    sol::table t = r->lua.create_table_with("point", h.point, "normal", h.normal, "distance", h.distance);
    t["entity"] = lua_id(h.entity);
    return t;
  };

  // --- sprites and sprite animation
  n["sprite_flip"] = [reg, ent](lua_Integer id, bool x, sol::optional<bool> y) {
    if (sprite *s = reg().valid(ent(id)) ? reg().try_get<sprite>(ent(id)) : nullptr) {
      s->flip_x = x;
      if (y)
        s->flip_y = *y;
    }
  };
  n["sprite_visible"] = [reg, ent](lua_Integer id, bool on) {
    if (sprite *s = reg().valid(ent(id)) ? reg().try_get<sprite>(ent(id)) : nullptr)
      s->visible = on;
  };
  n["sprite_tint"] = [reg, ent](lua_Integer id, f32 r_, f32 g, f32 b, sol::optional<f32> a) {
    if (sprite *s = reg().valid(ent(id)) ? reg().try_get<sprite>(ent(id)) : nullptr)
      s->tint = {r_, g, b, a.value_or(1.0f)};
  };
  const auto animator_of = [reg, ent](lua_Integer id) {
    return reg().valid(ent(id)) ? reg().try_get<animator>(ent(id)) : nullptr;
  };
  n["anim_play"] = [c, animator_of](lua_Integer id, const std::string &clip, sol::optional<bool> restart) {
    animator *a = animator_of(id);
    if (a == nullptr)
      return false;
    a->playing = true;
    return animator_play(*c, *a, clip.c_str(), restart.value_or(false));
  };
  n["anim_stop"] = [animator_of](lua_Integer id) {
    if (animator *a = animator_of(id))
      a->playing = false;
  };
  n["anim_resume"] = [animator_of](lua_Integer id) {
    if (animator *a = animator_of(id))
      a->playing = true;
  };
  n["anim_current"] = [c, animator_of](lua_Integer id) -> sol::optional<std::string> {
    const animator *a = animator_of(id);
    const char *name = a ? animator_current(*c, *a) : nullptr;
    return name ? sol::optional<std::string>(name) : sol::nullopt;
  };
  n["anim_set"] = [c, animator_of](lua_Integer id, const std::string &param, sol::object value) {
    animator *a = animator_of(id);
    if (a == nullptr)
      return;
    if (value.get_type() == sol::type::boolean)
      animator_set_bool(*c, *a, param.c_str(), value.as<bool>());
    else if (value.get_type() == sol::type::number)
      animator_set(*c, *a, param.c_str(), value.as<f32>());
  };
  n["anim_trigger"] = [c, animator_of](lua_Integer id, const std::string &param) {
    if (animator *a = animator_of(id))
      animator_trigger(*c, *a, param.c_str());
  };

  // --- 2D camera
  n["camera_spawn"] = [c, lua_id](sol::optional<f32> zoom, sol::optional<vec2> pos) {
    return lua_id(camera_spawn(*c, zoom.value_or(1.0f), pos.value_or(vec2{})));
  };
  n["camera_follow"] = [reg, ent](lua_Integer camera, lua_Integer target, sol::optional<sol::table> opts) {
    entt::registry &w = reg();
    if (!w.valid(ent(camera)))
      return;
    camera_follow &f = w.get_or_emplace<camera_follow>(ent(camera));
    f.target = ent(target);
    if (!opts)
      return;
    const sol::table &o = *opts;
    if (sol::optional<vec2> v = o["offset"])
      f.offset = *v;
    if (sol::optional<vec2> v = o["deadzone"])
      f.deadzone = *v;
    if (sol::optional<vec2> v = o["lookahead"])
      f.lookahead = *v;
    if (sol::optional<f32> s = o["smoothing"])
      f.smoothing = *s;
    if (sol::optional<bool> b = o["pixel_snap"])
      f.pixel_snap = *b;
    if (sol::optional<sol::table> b = o["bounds"])
      f.bounds = rect{{(*b)["x"].get_or(0.0f), (*b)["y"].get_or(0.0f)},
                      {(*b)["w"].get_or(0.0f), (*b)["h"].get_or(0.0f)}};
  };

  // --- tilemaps (an entity with transform and tilemap; the transform is its origin)
  struct map_ref {
    tilemap *map = nullptr;
    vec2 origin{};
  };
  const auto map_of = [reg, ent](lua_Integer id) {
    map_ref m;
    entt::registry &w = reg();
    if (!w.valid(ent(id)))
      return m;
    m.map = w.try_get<tilemap>(ent(id));
    if (const transform *t = w.try_get<transform>(ent(id)))
      m.origin = t->pos;
    return m;
  };
  n["tile_get"] = [map_of](lua_Integer id, i32 x, i32 y) {
    const map_ref m = map_of(id);
    return m.map ? tilemap_get(*m.map, x, y) : -1;
  };
  n["tile_set"] = [map_of](lua_Integer id, i32 x, i32 y, i32 tile) {
    if (const map_ref m = map_of(id); m.map)
      tilemap_set(*m.map, x, y, tile);
  };
  n["tile_cell"] = [map_of](lua_Integer id, vec2 world_pos) {
    const map_ref m = map_of(id);
    const cell at = m.map ? tilemap_cell_at(*m.map, m.origin, world_pos) : cell{};
    return std::make_tuple(at.x, at.y);
  };
  n["tile_solid"] = [map_of](lua_Integer id, vec2 world_pos) {
    const map_ref m = map_of(id);
    if (!m.map)
      return false;
    const cell at = tilemap_cell_at(*m.map, m.origin, world_pos);
    const tile_shape s = tilemap_shape(*m.map, tilemap_get(*m.map, at.x, at.y));
    return s != tile_none;
  };

  // --- 2D particles
  n["particles_burst"] = [reg, ent](lua_Integer id, i32 count) {
    if (particle_emitter *p = reg().valid(ent(id)) ? reg().try_get<particle_emitter>(ent(id)) : nullptr)
      particles_burst(*p, count);
  };
  n["particles_spawn"] = [c, reg, ent, lua_id](lua_Integer preset, vec2 at, i32 count) -> sol::optional<lua_Integer> {
    const particle_emitter *p = reg().valid(ent(preset)) ? reg().try_get<particle_emitter>(ent(preset)) : nullptr;
    if (p == nullptr)
      return sol::nullopt;
    const particle_emitter copy = *p;
    return lua_id(particles_spawn(*c, copy, at, count));
  };
}
} // namespace njin

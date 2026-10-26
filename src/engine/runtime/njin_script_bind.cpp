// The `njin` Lua module: thin bindings over the public C++ API, nothing more.
// Time and entities here; the rest in njin_script_bind_vec.cpp,
// njin_script_bind_io.cpp, njin_script_bind_2d.cpp and njin_script_bind_more.cpp.
#include "njin_script_rt.h"

#include "_comps.h"
#include "_math.h"
#include "njin_cfg.h"
#include "njin_ctx.h"
#include "njin_scene.h"
#include "njin_3d.h"
#include <cstring>

namespace njin {
void script_bind_njin(context &ctx, script_runtime &rt) {
  sol::state &lua = rt.lua;
  script_bind_vectors(lua);
  sol::table n = lua.create_named_table("njin");
  n["vec2"] = lua["vec2"];
  n["vec3"] = lua["vec3"];
  context *c = &ctx;
  const auto reg = [c]() -> entt::registry & { return world(*c); };
  const auto ent = [](lua_Integer v) { return script_entity(v); };
  const auto lua_id = [](entt::entity e) -> sol::optional<lua_Integer> {
    if (e == entt::null)
      return sol::nullopt;
    return (lua_Integer)entt::to_integral(e);
  };

  // --- time and randomness
  n["delta"] = [c] { return delta(*c); };
  n["elapsed"] = [c] { return elapsed(*c); };
  n["time_scale"] = [c] { return time_scale(*c); };
  n["set_time_scale"] = [c](f32 s) { time_set_scale(*c, s); };
  n["random"] = [c] { return random(*c).unit(); };
  n["random_range"] = [c](f32 lo, f32 hi) { return random(*c).range(lo, hi); };
  n["random_int"] = [c](i32 lo, i32 hi) { return random(*c).range(lo, hi); };
  // The machine's clock: Unix seconds, and a date as a table of the date_time
  // fields.
  script_runtime *r = &rt;
  n["time_unix"] = [] { return (lua_Integer)time_unix(); };
  n["time_date"] = [r](lua_Integer t, sol::optional<bool> local) {
    const date_time d = time_date((i64)t, local.value_or(true));
    return r->lua.create_table_with("year", d.year, "month", d.month, "day", d.day, "hour", d.hour, "minute",
                                    d.minute, "second", d.second, "weekday", d.weekday);
  };
  n["time_from_date"] = [](sol::table d, sol::optional<bool> local) {
    const date_time date{.year = d.get_or("year", 1970),
                         .month = d.get_or("month", 1),
                         .day = d.get_or("day", 1),
                         .hour = d.get_or("hour", 0),
                         .minute = d.get_or("minute", 0),
                         .second = d.get_or("second", 0)};
    return (lua_Integer)time_from_date(date, local.value_or(true));
  };

  // --- entities and transforms
  n["entity_create"] = [c, reg, lua_id] {
    entt::registry &w = reg();
    const entt::entity e = w.create();
    const scene_handle s = scene_current(*c);
    if (s.id != 0)
      w.emplace<scene_owned>(e, s);
    return lua_id(e);
  };
  n["entity_destroy"] = [reg, ent](lua_Integer id) {
    entt::registry &w = reg();
    if (w.valid(ent(id)))
      w.destroy(ent(id));
  };
  n["entity_valid"] = [reg, ent](lua_Integer id) { return reg().valid(ent(id)); };
  n["position"] = [reg, ent](lua_Integer id) -> sol::optional<vec2> {
    const transform *t = reg().valid(ent(id)) ? reg().try_get<transform>(ent(id)) : nullptr;
    return t ? sol::optional<vec2>(t->pos) : sol::nullopt;
  };
  n["set_position"] = [reg, ent](lua_Integer id, vec2 p) {
    if (reg().valid(ent(id)))
      reg().get_or_emplace<transform>(ent(id)).pos = p;
  };
  n["rotation"] = [reg, ent](lua_Integer id) {
    const transform *t = reg().valid(ent(id)) ? reg().try_get<transform>(ent(id)) : nullptr;
    return t ? t->rot : 0.0f;
  };
  n["set_rotation"] = [reg, ent](lua_Integer id, f32 deg) {
    if (reg().valid(ent(id)))
      reg().get_or_emplace<transform>(ent(id)).rot = deg;
  };
  n["scale"] = [reg, ent](lua_Integer id) {
    const transform *t = reg().valid(ent(id)) ? reg().try_get<transform>(ent(id)) : nullptr;
    return t ? t->scale : 1.0f;
  };
  n["set_scale"] = [reg, ent](lua_Integer id, f32 s) {
    if (reg().valid(ent(id)))
      reg().get_or_emplace<transform>(ent(id)).scale = s;
  };
  n["position3d"] = [reg, ent](lua_Integer id) -> sol::optional<vec3> {
    const transform3d *t = reg().valid(ent(id)) ? reg().try_get<transform3d>(ent(id)) : nullptr;
    return t ? sol::optional<vec3>(t->position) : sol::nullopt;
  };
  n["set_position3d"] = [reg, ent](lua_Integer id, vec3 p) {
    if (reg().valid(ent(id)))
      reg().get_or_emplace<transform3d>(ent(id)).position = p;
  };
  n["rotation3d"] = [reg, ent](lua_Integer id) -> sol::optional<vec3> {
    const transform3d *t = reg().valid(ent(id)) ? reg().try_get<transform3d>(ent(id)) : nullptr;
    return t ? sol::optional<vec3>(t->rotation) : sol::nullopt;
  };
  n["set_rotation3d"] = [reg, ent](lua_Integer id, vec3 deg) {
    if (reg().valid(ent(id)))
      reg().get_or_emplace<transform3d>(ent(id)).rotation = deg;
  };
  n["scale3d"] = [reg, ent](lua_Integer id) -> sol::optional<vec3> {
    const transform3d *t = reg().valid(ent(id)) ? reg().try_get<transform3d>(ent(id)) : nullptr;
    return t ? sol::optional<vec3>(t->scale) : sol::nullopt;
  };
  n["set_scale3d"] = [reg, ent](lua_Integer id, vec3 s) {
    if (reg().valid(ent(id)))
      reg().get_or_emplace<transform3d>(ent(id)).scale = s;
  };

  script_bind_njin_io(ctx, rt, n);
  script_bind_njin_2d(ctx, rt, n);
}
} // namespace njin

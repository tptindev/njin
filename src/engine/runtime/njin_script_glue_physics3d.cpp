// The `njin` Lua module's 3D physics, glue between the script and physics3d
// features (njin_features.h). Thin bindings over the public API.
#include "njin_script_rt.h"

#include "_math.h"
#include "njin_cfg.h"
#include "njin_ctx.h"
#include "njin_3d.h"
#include "njin_physics3d.h"
#include "njin_ctx_get.h"
#include <algorithm>
#include <vector>

namespace njin {
namespace {
void script_bind_physics3d(context &ctx, script_runtime &rt) {
  sol::table n = rt.lua["njin"];
  context *c = &ctx;
  script_runtime *r = &rt;
  const auto reg = [c]() -> entt::registry & { return world(*c); };
  const auto ent = [](lua_Integer v) { return script_entity(v); };
  const auto lua_id = [](entt::entity e) -> sol::optional<lua_Integer> {
    if (e == entt::null)
      return sol::nullopt;
    return (lua_Integer)entt::to_integral(e);
  };

  // --- 3D physics
  n["raycast3d"] = [c, lua_id, r](vec3 origin, vec3 direction, sol::optional<f32> max_distance) -> sol::object {
    body3d_handle body{};
    const ray3d_hit hit = physics3d_raycast(*c, ray3d{origin, normalize(direction)}, max_distance.value_or(1000.0f), &body);
    if (!hit.hit)
      return sol::make_object(r->lua, sol::lua_nil);
    sol::table t = r->lua.create_table_with("point", hit.point, "normal", hit.normal, "distance", hit.distance,
                                            "body", (lua_Integer)body.id);
    entt::registry &w = world(*c);
    for (auto [e, b] : w.view<const body3d>().each())
      if (b.handle.id == body.id) {
        t["entity"] = lua_id(e);
        break;
      }
    return t;
  };
  const auto body_of = [reg, ent](lua_Integer id) -> body3d_handle {
    const body3d *b = reg().valid(ent(id)) ? reg().try_get<body3d>(ent(id)) : nullptr;
    return b ? b->handle : body3d_handle{};
  };
  n["body_velocity"] = [c, body_of](lua_Integer id) { return body3d_velocity(*c, body_of(id)); };
  n["body_set_velocity"] = [c, body_of](lua_Integer id, vec3 v) { body3d_set_velocity(*c, body_of(id), v); };
  n["body_impulse"] = [c, body_of](lua_Integer id, vec3 v) { body3d_add_impulse(*c, body_of(id), v); };
  const auto character_of = [reg, ent](lua_Integer id) -> character3d_handle {
    const character3d *ch = reg().valid(ent(id)) ? reg().try_get<character3d>(ent(id)) : nullptr;
    return ch ? ch->handle : character3d_handle{};
  };
  n["character_move"] = [c, character_of](lua_Integer id, vec3 v) {
    character3d_set_velocity(*c, character_of(id), v);
  };
  n["character_position"] = [c, character_of](lua_Integer id) {
    return character3d_position(*c, character_of(id));
  };
  n["character_grounded"] = [c, character_of](lua_Integer id) {
    return character3d_grounded(*c, character_of(id));
  };
}
} // namespace

void feature_install_script_physics3d(context &ctx) { ctx_ref<script_state>(ctx)->binders.push_back(script_bind_physics3d); }
} // namespace njin

// The `njin` Lua module's 3D navigation, glue between the script and nav3d
// features (njin_features.h). Thin bindings over the public API.
#include "njin_script_rt.h"

#include "_math.h"
#include "njin_cfg.h"
#include "njin_nav3d.h"
#include "njin_ctx_impl.h"
#include <algorithm>
#include <vector>

namespace njin {
namespace {
void script_bind_nav3d(context &ctx, script_runtime &rt) {
  sol::table n = rt.lua["njin"];
  context *c = &ctx;

  // --- 3D navmesh (handles from C++: navmesh3d_handle::id, nav3d_agent_handle::id)
  n["nav3d_path"] = [c](lua_Integer mesh, vec3 from, vec3 to,
                        sol::optional<lua_Integer> filter) -> sol::optional<sol::as_table_t<std::vector<vec3>>> {
    std::vector<vec3> out;
    if (!navmesh3d_path(*c, navmesh3d_handle{(u32)mesh}, from, to, out, (i32)filter.value_or(0)))
      return sol::nullopt;
    return sol::as_table(std::move(out));
  };
  // nav3d_set_filter(mesh, index, {cost = {[area] = c, ...}, exclude = {area, ...}})
  n["nav3d_set_filter"] = [c](lua_Integer mesh, lua_Integer index, sol::table t) {
    nav3d_filter f{};
    if (sol::optional<sol::table> cost = t["cost"])
      for (const auto &[k, v] : *cost)
        if (k.is<lua_Integer>() && v.is<f32>()) {
          const lua_Integer a = k.as<lua_Integer>();
          if (a >= 0 && a < nav3d_max_areas)
            f.cost[a] = v.as<f32>();
        }
    if (sol::optional<sol::table> ex = t["exclude"])
      for (const auto &[k, v] : *ex)
        if (v.is<lua_Integer>()) {
          const lua_Integer a = v.as<lua_Integer>();
          if (a >= 0 && a < nav3d_max_areas)
            f.excluded = (u16)(f.excluded | (1u << a));
        }
    navmesh3d_set_filter(*c, navmesh3d_handle{(u32)mesh}, (i32)index, f);
  };
  n["nav3d_agent_filter"] = [c](lua_Integer agent, lua_Integer filter) {
    nav3d_agent_set_filter(*c, nav3d_agent_handle{(u32)agent}, (i32)filter);
  };
  n["nav3d_add_area"] = [c](lua_Integer mesh, vec3 center, vec3 size, f32 yaw, lua_Integer area) {
    return navmesh3d_add_area(*c, navmesh3d_handle{(u32)mesh}, center, size, yaw,
                              (u8)std::clamp<lua_Integer>(area, 0, 255));
  };
  n["nav3d_remove_area"] = [c](lua_Integer mesh, lua_Integer id) {
    navmesh3d_remove_area(*c, navmesh3d_handle{(u32)mesh}, (i32)id);
  };
  // nav3d_add_obstacle(mesh, {position = vec3, size = vec3, yaw = 0, radius = 0})
  n["nav3d_add_obstacle"] = [c](lua_Integer mesh, sol::table t) {
    nav3d_obstacle_desc d{};
    if (sol::optional<vec3> p = t["position"])
      d.position = *p;
    if (sol::optional<vec3> s = t["size"])
      d.size = *s;
    d.yaw = t["yaw"].get_or(0.0f);
    d.radius = t["radius"].get_or(0.0f);
    return navmesh3d_add_obstacle(*c, navmesh3d_handle{(u32)mesh}, d);
  };
  n["nav3d_move_obstacle"] = [c](lua_Integer mesh, lua_Integer id, vec3 position, sol::optional<f32> yaw) {
    return navmesh3d_move_obstacle(*c, navmesh3d_handle{(u32)mesh}, (i32)id, position, yaw.value_or(0.0f));
  };
  n["nav3d_remove_obstacle"] = [c](lua_Integer mesh, lua_Integer id) {
    navmesh3d_remove_obstacle(*c, navmesh3d_handle{(u32)mesh}, (i32)id);
  };
  n["nav3d_set_target"] = [c](lua_Integer agent, vec3 target) {
    return nav3d_agent_set_target(*c, nav3d_agent_handle{(u32)agent}, target);
  };
  n["nav3d_stop"] = [c](lua_Integer agent) { nav3d_agent_stop(*c, nav3d_agent_handle{(u32)agent}); };
  n["nav3d_position"] = [c](lua_Integer agent) { return nav3d_agent_position(*c, nav3d_agent_handle{(u32)agent}); };
  n["nav3d_velocity"] = [c](lua_Integer agent) { return nav3d_agent_velocity(*c, nav3d_agent_handle{(u32)agent}); };
  n["nav3d_arrived"] = [c](lua_Integer agent) { return nav3d_agent_arrived(*c, nav3d_agent_handle{(u32)agent}); };
}
} // namespace

void feature_install_script_nav3d(context &ctx) { ctx.script->binders.push_back(script_bind_nav3d); }
} // namespace njin

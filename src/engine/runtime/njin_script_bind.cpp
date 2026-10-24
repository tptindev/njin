// The `njin` Lua module: thin bindings over the public C++ API, nothing more.
// Vectors, time and entities here; the rest in njin_script_bind_io.cpp,
// njin_script_bind_2d.cpp and njin_script_bind_more.cpp.
#include "njin_script_rt.h"

#include "njin.h"
#include <cstring>

namespace njin {
namespace {
void bind_vectors(sol::state &lua) {
  lua.new_usertype<vec2>(
      "vec2", sol::call_constructor,
      sol::factories([](sol::optional<f32> x, sol::optional<f32> y) { return vec2{x.value_or(0.0f), y.value_or(0.0f)}; }),
      "x", &vec2::x, "y", &vec2::y, sol::meta_function::addition, [](vec2 a, vec2 b) { return a + b; },
      sol::meta_function::subtraction, [](vec2 a, vec2 b) { return a - b; }, sol::meta_function::multiplication,
      sol::overload([](vec2 a, vec2 b) { return a * b; }, [](vec2 a, f32 s) { return a * s; },
                    [](f32 s, vec2 a) { return a * s; }),
      sol::meta_function::division,
      sol::overload([](vec2 a, vec2 b) { return a / b; }, [](vec2 a, f32 s) { return a / s; }),
      sol::meta_function::unary_minus, [](vec2 a) { return -a; }, sol::meta_function::equal_to,
      [](vec2 a, vec2 b) { return a.x == b.x && a.y == b.y; }, sol::meta_function::to_string,
      [](vec2 a) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "vec2(%g, %g)", a.x, a.y);
        return std::string(buf);
      },
      "length", [](vec2 a) { return length(a); }, "normalized", [](vec2 a) { return normalize(a); }, "dot",
      [](vec2 a, vec2 b) { return dot(a, b); });
  lua.new_usertype<vec3>(
      "vec3", sol::call_constructor,
      sol::factories([](sol::optional<f32> x, sol::optional<f32> y, sol::optional<f32> z) {
        return vec3{x.value_or(0.0f), y.value_or(0.0f), z.value_or(0.0f)};
      }),
      "x", &vec3::x, "y", &vec3::y, "z", &vec3::z, sol::meta_function::addition,
      [](vec3 a, vec3 b) { return a + b; }, sol::meta_function::subtraction, [](vec3 a, vec3 b) { return a - b; },
      sol::meta_function::multiplication,
      sol::overload([](vec3 a, vec3 b) { return vec3{a.x * b.x, a.y * b.y, a.z * b.z}; },
                    [](vec3 a, f32 s) { return a * s; }, [](f32 s, vec3 a) { return a * s; }),
      sol::meta_function::division,
      sol::overload([](vec3 a, vec3 b) { return vec3{a.x / b.x, a.y / b.y, a.z / b.z}; },
                    [](vec3 a, f32 s) { return a * (1.0f / s); }),
      sol::meta_function::unary_minus, [](vec3 a) { return vec3{} - a; }, sol::meta_function::equal_to,
      [](vec3 a, vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }, sol::meta_function::to_string,
      [](vec3 a) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "vec3(%g, %g, %g)", a.x, a.y, a.z);
        return std::string(buf);
      },
      "length", [](vec3 a) { return length(a); }, "normalized", [](vec3 a) { return normalize(a); }, "dot",
      [](vec3 a, vec3 b) { return dot(a, b); }, "cross", [](vec3 a, vec3 b) { return cross(a, b); });
}
} // namespace

void script_bind_njin(context &ctx, script_runtime &rt) {
  sol::state &lua = rt.lua;
  bind_vectors(lua);
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

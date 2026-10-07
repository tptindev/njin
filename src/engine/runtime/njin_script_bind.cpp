// The `njin` Lua module: thin bindings over the public C++ API, nothing more.
#include "njin_script_rt.h"

#include "njin.h"
#include <cstring>

namespace njin {
namespace {
struct key_entry {
  const char *name;
  i32 code;
};

constexpr key_entry keys[] = {
    {"a", key_a}, {"b", key_b}, {"c", key_c}, {"d", key_d}, {"e", key_e}, {"f", key_f}, {"g", key_g},
    {"h", key_h}, {"i", key_i}, {"j", key_j}, {"k", key_k}, {"l", key_l}, {"m", key_m}, {"n", key_n},
    {"o", key_o}, {"p", key_p}, {"q", key_q}, {"r", key_r}, {"s", key_s}, {"t", key_t}, {"u", key_u},
    {"v", key_v}, {"w", key_w}, {"x", key_x}, {"y", key_y}, {"z", key_z}, {"0", key_0}, {"1", key_1},
    {"2", key_2}, {"3", key_3}, {"4", key_4}, {"5", key_5}, {"6", key_6}, {"7", key_7}, {"8", key_8},
    {"9", key_9}, {"f1", key_f1}, {"f2", key_f2}, {"f3", key_f3}, {"f4", key_f4}, {"f5", key_f5},
    {"f6", key_f6}, {"f7", key_f7}, {"f8", key_f8}, {"f9", key_f9}, {"f10", key_f10}, {"f11", key_f11},
    {"f12", key_f12}, {"up", key_up}, {"down", key_down}, {"left", key_left}, {"right", key_right},
    {"home", key_home}, {"end", key_end}, {"page_up", key_page_up}, {"page_down", key_page_down},
    {"insert", key_insert}, {"delete", key_delete}, {"space", key_space}, {"enter", key_enter},
    {"tab", key_tab}, {"escape", key_escape}, {"backspace", key_backspace}, {"left_shift", key_left_shift},
    {"left_control", key_left_control}, {"left_alt", key_left_alt}, {"right_shift", key_right_shift},
    {"right_control", key_right_control}, {"right_alt", key_right_alt}, {"comma", key_comma},
    {"period", key_period}, {"minus", key_minus}, {"equal", key_equal}, {"slash", key_slash},
    {"semicolon", key_semicolon}, {"apostrophe", key_apostrophe}, {"grave", key_grave},
    {"kp_0", key_kp_0}, {"kp_1", key_kp_1}, {"kp_2", key_kp_2}, {"kp_3", key_kp_3}, {"kp_4", key_kp_4},
    {"kp_5", key_kp_5}, {"kp_6", key_kp_6}, {"kp_7", key_kp_7}, {"kp_8", key_kp_8}, {"kp_9", key_kp_9},
    {"kp_enter", key_kp_enter}, {"kp_add", key_kp_add}, {"kp_subtract", key_kp_subtract},
};

key_code key_of(const std::string &name) {
  for (const key_entry &k : keys)
    if (name == k.name)
      return (key_code)k.code;
  return key_none;
}

mouse_button mouse_of(const std::string &name) {
  if (name == "right")
    return mouse_right;
  if (name == "middle")
    return mouse_middle;
  return mouse_left;
}

ease ease_of(const sol::optional<std::string> &name) {
  static constexpr std::pair<const char *, ease> eases[] = {
      {"linear", ease::linear},       {"in_quad", ease::in_quad},       {"out_quad", ease::out_quad},
      {"in_out_quad", ease::in_out_quad}, {"in_cubic", ease::in_cubic},   {"out_cubic", ease::out_cubic},
      {"in_out_cubic", ease::in_out_cubic}, {"in_sine", ease::in_sine},   {"out_sine", ease::out_sine},
      {"in_out_sine", ease::in_out_sine}, {"in_back", ease::in_back},     {"out_back", ease::out_back},
      {"out_bounce", ease::out_bounce}, {"out_elastic", ease::out_elastic}};
  if (name)
    for (const auto &[n, e] : eases)
      if (*name == n)
        return e;
  return ease::out_quad;
}

rgba color_of(sol::optional<f32> r, sol::optional<f32> g, sol::optional<f32> b, sol::optional<f32> a) {
  return {r.value_or(1.0f), g.value_or(1.0f), b.value_or(1.0f), a.value_or(1.0f)};
}

// A Lua callback stored in a timer or a tween: errors are logged, never thrown.
sol::protected_function callback(script_runtime &rt, const sol::protected_function &fn) {
  sol::protected_function f = fn;
  f.set_error_handler(rt.traceback);
  return f;
}

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
  script_runtime *r = &rt;
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

  // --- input
  n["key_pressed"] = [c](const std::string &k) { return key_pressed(*c, key_of(k)); };
  n["key_held"] = [c](const std::string &k) { return key_held(*c, key_of(k)); };
  n["key_released"] = [c](const std::string &k) { return key_released(*c, key_of(k)); };
  n["mouse_pos"] = [c] { return mouse_pos(*c); };
  n["mouse_delta"] = [c] { return mouse_delta(*c); };
  n["mouse_wheel"] = [c] { return mouse_wheel(*c); };
  n["mouse_pressed"] = [c](sol::optional<std::string> b) { return mouse_pressed(*c, mouse_of(b.value_or("left"))); };
  n["mouse_held"] = [c](sol::optional<std::string> b) { return mouse_held(*c, mouse_of(b.value_or("left"))); };
  n["mouse_released"] = [c](sol::optional<std::string> b) {
    return mouse_released(*c, mouse_of(b.value_or("left")));
  };
  n["action_pressed"] = [c](const std::string &a) { return action_pressed(*c, action_find(*c, a.c_str())); };
  n["action_held"] = [c](const std::string &a) { return action_held(*c, action_find(*c, a.c_str())); };
  n["action_released"] = [c](const std::string &a) { return action_released(*c, action_find(*c, a.c_str())); };
  n["axis"] = [c](const std::string &a) { return axis_value(*c, axis_find(*c, a.c_str())); };

  // --- timers and tweens (owned by the current scene, like their C++ forms)
  n["after"] = [c, r](f32 seconds, sol::protected_function fn) {
    sol::protected_function f = callback(*r, fn);
    return (lua_Integer)timer_after(*c, seconds, [f](context &) { script_pcall(f, "njin.after"); }).id;
  };
  n["every"] = [c, r](f32 interval, sol::protected_function fn, sol::optional<i32> count) {
    sol::protected_function f = callback(*r, fn);
    return (lua_Integer)timer_every(*c, interval, [f](context &) { script_pcall(f, "njin.every"); },
                                    count.value_or(-1))
        .id;
  };
  n["cancel"] = [c](lua_Integer id) { timer_cancel(*c, timer_handle{.id = (u32)id}); };
  n["tween_move"] = [c, ent](lua_Integer id, vec2 to, f32 seconds, sol::optional<std::string> curve) {
    return (lua_Integer)tween_move(*c, ent(id), to, seconds, ease_of(curve)).id;
  };
  n["tween_scale"] = [c, ent](lua_Integer id, f32 to, f32 seconds, sol::optional<std::string> curve) {
    return (lua_Integer)tween_scale(*c, ent(id), to, seconds, ease_of(curve)).id;
  };
  n["tween_rotate"] = [c, ent](lua_Integer id, f32 to, f32 seconds, sol::optional<std::string> curve) {
    return (lua_Integer)tween_rotate(*c, ent(id), to, seconds, ease_of(curve)).id;
  };
  n["tween_value"] = [c, r](f32 from, f32 to, f32 seconds, sol::protected_function fn,
                             sol::optional<std::string> curve) {
    sol::protected_function f = callback(*r, fn);
    return (lua_Integer)tween_value(*c, from, to, seconds,
                                    [f](context &, f32 v) { script_pcall(f, "njin.tween_value", v); },
                                    ease_of(curve))
        .id;
  };
  n["tween_cancel"] = [c](lua_Integer id) { tween_cancel(*c, tween_handle{.id = (u32)id}); };

  // --- sound
  n["sound_load"] = [c](const std::string &path) { return (lua_Integer)sound_load(*c, path.c_str()).id; };
  n["sound_play"] = [c](lua_Integer id) { sound_play_once(*c, sound_handle{.id = (u32)id}); };
  n["sound_play_at"] = [c](lua_Integer id, vec2 at) { sound_play_at(*c, sound_handle{.id = (u32)id}, at); };
  n["sound_play3d"] = sol::overload(
      [c](lua_Integer id, vec3 at) { sound_play3d(*c, sound_handle{.id = (u32)id}, at); },
      [c, ent](lua_Integer id, lua_Integer entity) { sound_play3d(*c, sound_handle{.id = (u32)id}, ent(entity)); });
  n["sound_stop"] = [c](lua_Integer id) { sound_stop(*c, sound_handle{.id = (u32)id}); };

  // --- scenes
  n["scene_set"] = [c](const std::string &name) {
    const scene_handle s = scene_find(*c, name.c_str());
    if (s.id != 0)
      scene_set(*c, s);
    return s.id != 0;
  };
  n["scene_fade"] = [c](const std::string &name) {
    const scene_handle s = scene_find(*c, name.c_str());
    if (s.id != 0)
      scene_fade(*c, s);
    return s.id != 0;
  };

  // --- 2D drawing (in on_render)
  n["draw_rect"] = [c](f32 x, f32 y, f32 w, f32 h, sol::optional<f32> r_, sol::optional<f32> g, sol::optional<f32> b,
                       sol::optional<f32> a) { draw_rect(*c, rect{{x, y}, {w, h}}, color_of(r_, g, b, a)); };
  n["draw_circle"] = [c](vec2 center, f32 radius, sol::optional<f32> r_, sol::optional<f32> g, sol::optional<f32> b,
                         sol::optional<f32> a) { draw_circle(*c, center, radius, color_of(r_, g, b, a)); };
  n["draw_line"] = [c](vec2 from, vec2 to, f32 thickness, sol::optional<f32> r_, sol::optional<f32> g,
                       sol::optional<f32> b, sol::optional<f32> a) {
    draw_line(*c, from, to, thickness, color_of(r_, g, b, a));
  };
  n["draw_text"] = [c](const std::string &text, vec2 at, f32 size, sol::optional<f32> r_, sol::optional<f32> g,
                       sol::optional<f32> b, sol::optional<f32> a) {
    draw_text(*c, text.c_str(), at, size, color_of(r_, g, b, a));
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

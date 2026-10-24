// The `njin` Lua module: input, timers and tweens, sound, scenes and 2D
// drawing. Thin bindings over the public API, split from njin_script_bind.cpp
// so the parts compile in parallel.
#include "njin_script_rt.h"

#include "_collide.h"
#include "_math.h"
#include "_tilemap.h"
#include "_tween.h"
#include "njin_cfg.h"
#include "njin_audio.h"
#include "njin_draw.h"
#include "njin_input.h"
#include "njin_scene.h"
#include "njin_timer.h"

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

} // namespace

void script_bind_njin_io(context &ctx, script_runtime &rt, sol::table n) {
  context *c = &ctx;
  script_runtime *r = &rt;
  const auto ent = [](lua_Integer v) { return script_entity(v); };

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
}
} // namespace njin

// The `njin` Lua module, part two: 2D lighting, immediate UI, text and
// splines. Thin bindings over the public API. The 3D part is njin_script_glue_3d.cpp.
#include "njin_script_rt.h"

#include "njin.h"
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

spline_end end_of(const sol::object &o) {
  if (o.get_type() == sol::type::string) {
    const std::string e = o.as<std::string>();
    if (e == "loop")
      return spline_loop;
    if (e == "ping_pong")
      return spline_ping_pong;
  }
  return spline_stop;
}

const char *end_name(spline_end e) {
  return e == spline_loop ? "loop" : e == spline_ping_pong ? "ping_pong" : "stop";
}
} // namespace

void script_bind_njin_more(context &ctx, script_runtime &rt, sol::table n) {
  context *c = &ctx;
  script_runtime *r = &rt;

  // --- 2D lights: lighting_set() and a light_2d component on an entity
  n["lighting_set"] = [c](sol::table t) {
    lighting_desc d = lighting_get(*c);
    d.enabled = flag(t, "enabled", true);
    d.ambient = color_of(t["ambient"], d.ambient);
    d.exposure = num(t, "exposure", d.exposure);
    lighting_set(*c, d);
  };
  n["light2d_set"] = [c](sol::this_state s, lua_Integer entity, sol::table t) {
    entt::registry &w = world(*c);
    const entt::entity e = script_entity(entity);
    if (!w.valid(e))
      fail(s, "njin.light2d_set: %lld is not an entity", (long long)entity);
    light_2d &l = w.get_or_emplace<light_2d>(e);
    const std::string kind = t["kind"].get_or(std::string(l.kind == light_spot ? "spot"
                                                          : l.kind == light_directional ? "directional"
                                                                                        : "point"));
    l.kind = kind == "spot" ? light_spot : kind == "directional" ? light_directional : light_point;
    l.color = color_of(t["color"], l.color);
    l.temperature = num(t, "temperature", l.temperature);
    l.intensity = num(t, "intensity", l.intensity);
    l.radius = num(t, "radius", l.radius);
    l.size = num(t, "size", l.size);
    l.angle = num(t, "angle", l.angle);
    l.cone = num(t, "cone", l.cone);
    l.softness = num(t, "softness", l.softness);
    l.height = num(t, "height", l.height);
    l.elevation = num(t, "elevation", l.elevation);
    l.cast_shadows = flag(t, "cast_shadows", l.cast_shadows);
    l.enabled = flag(t, "enabled", l.enabled);
    static_cast<void>(w.get_or_emplace<transform>(e));
  };

  // --- immediate UI (on_ui, screen space)
  n["ui_begin"] = [c](sol::optional<sol::table> t) {
    std::string id = "panel", title;
    ui_panel_desc d{};
    if (t) {
      id = (*t)["id"].get_or(id);
      title = (*t)["title"].get_or(std::string());
      d.anchor = vec2_of((*t)["anchor"], d.anchor);
      d.pivot = vec2_of((*t)["pivot"], d.pivot);
      d.offset = vec2_of((*t)["offset"], d.offset);
      d.width = num(*t, "width", d.width);
      d.background = flag(*t, "background", d.background);
      d.navigable = flag(*t, "navigable", d.navigable);
    }
    d.id = id.c_str();
    d.title = title.empty() ? nullptr : title.c_str();
    ui_begin(*c, d);
  };
  n["ui_end"] = [c] { ui_end(*c); };
  n["ui_row"] = [c](i32 columns) { ui_row(*c, columns); };
  n["ui_label"] = [c](const std::string &text) { ui_label(*c, text.c_str()); };
  n["ui_space"] = [c](f32 height) { ui_space(*c, height); };
  n["ui_button"] = [c](const std::string &label, sol::optional<bool> enabled) {
    return ui_button(*c, label.c_str(), enabled.value_or(true));
  };
  // changed, value = ui_toggle(label, value)
  n["ui_toggle"] = [c](const std::string &label, bool value) {
    const bool changed = ui_toggle(*c, label.c_str(), value);
    return std::make_tuple(changed, value);
  };
  // changed, value = ui_slider(label, value, min, max, step, percent)
  n["ui_slider"] = [c](const std::string &label, f32 value, f32 lo, f32 hi, sol::optional<f32> step,
                       sol::optional<bool> percent) {
    const bool changed = ui_slider(*c, label.c_str(), value, lo, hi, step.value_or(0.0f), percent.value_or(false));
    return std::make_tuple(changed, value);
  };
  // changed, index = ui_choice(label, index (1-based), {"a", "b", ...})
  n["ui_choice"] = [c](const std::string &label, i32 index, sol::table options) {
    std::vector<std::string> list;
    for (usize i = 1; i <= options.size(); i++)
      list.push_back(options[i].get_or(std::string()));
    i32 at = index - 1;
    const bool changed = ui_choice(*c, label.c_str(), at, std::span<const std::string>(list));
    return std::make_tuple(changed, at + 1);
  };
  n["ui_progress"] = [c](f32 value, sol::optional<std::string> text) {
    ui_progress(*c, value, text ? text->c_str() : nullptr);
  };
  // changed, text = ui_input(label, text, hint, max_chars)
  n["ui_input"] = [c](const std::string &label, std::string text, sol::optional<std::string> hint,
                      sol::optional<i32> max_chars) {
    const bool changed = ui_input(*c, label.c_str(), text,
                                  {.hint = hint ? hint->c_str() : nullptr, .max_chars = max_chars.value_or(64)});
    return std::make_tuple(changed, text);
  };
  n["ui_typing"] = [c] { return ui_typing(*c); };
  n["ui_back"] = [c] { return ui_back(*c); };
  n["ui_active"] = [c] { return ui_active(*c); };
  n["ui_mouse_over"] = [c] { return ui_mouse_over(*c); };
  // x, y, w, h of the last widget, in screen pixels.
  n["ui_last_rect"] = [c] {
    const rect a = ui_last_rect(*c);
    return std::make_tuple(a.pos.x, a.pos.y, a.size.x, a.size.y);
  };
  n["ui_toast"] = [c](const std::string &text, sol::optional<f32> seconds) {
    ui_toast(*c, text.c_str(), {.seconds = seconds.value_or(0.0f)});
  };

  // --- text: fonts and translations
  n["font_load"] = [c](const std::string &path, sol::optional<i32> size) {
    return (lua_Integer)font_load(*c, path.c_str(), size.value_or(0)).id;
  };
  n["draw_text_font"] = [c](const std::string &text, vec2 at, f32 size, lua_Integer font, sol::object color) {
    draw_text(*c, text.c_str(), at, size, color_of(color), font_handle{(u32)font});
  };
  n["text_measure"] = [c](const std::string &text, f32 size, sol::optional<lua_Integer> font) {
    return text_measure(*c, text.c_str(), size, font_handle{(u32)font.value_or(0)});
  };
  n["tr"] = [c](const std::string &key) { return std::string(tr(*c, key.c_str())); };
  n["trf"] = [c](const std::string &key, sol::variadic_args args) {
    std::vector<std::string> a;
    for (const auto &v : args) {
      if (v.get_type() == sol::type::string) {
        a.push_back(v.as<std::string>());
      } else if (v.get_type() == sol::type::number) {
        // Integers print without a fraction: "3 coins", not "3.000000 coins".
        const double d = v.as<double>();
        char buf[48];
        if (d == (double)(long long)d)
          std::snprintf(buf, sizeof buf, "%lld", (long long)d);
        else
          std::snprintf(buf, sizeof buf, "%g", d);
        a.push_back(buf);
      } else {
        a.push_back(std::string());
      }
    }
    const auto sv = [&](usize i) { return i < a.size() ? std::string_view(a[i]) : std::string_view(); };
    switch (a.size()) {
    case 0:
      return trf(*c, key.c_str(), {});
    case 1:
      return trf(*c, key.c_str(), {sv(0)});
    case 2:
      return trf(*c, key.c_str(), {sv(0), sv(1)});
    case 3:
      return trf(*c, key.c_str(), {sv(0), sv(1), sv(2)});
    case 4:
      return trf(*c, key.c_str(), {sv(0), sv(1), sv(2), sv(3)});
    case 5:
      return trf(*c, key.c_str(), {sv(0), sv(1), sv(2), sv(3), sv(4)});
    default:
      return trf(*c, key.c_str(), {sv(0), sv(1), sv(2), sv(3), sv(4), sv(5)});
    }
  };

  // --- splines (kept by id; destroyed by spline_destroy() or with their owner entity)
  const auto sweep = [c, r] {
    entt::registry &w = world(*c);
    for (auto it = r->splines.begin(); it != r->splines.end();)
      it = (it->second.owner != entt::null && !w.valid(it->second.owner)) ? r->splines.erase(it) : std::next(it);
  };
  const auto spline_at = [r, sweep](lua_State *L, lua_Integer id, const char *fn) -> script_spline & {
    sweep();
    const auto it = r->splines.find((u32)id);
    if (id <= 0 || it == r->splines.end())
      fail(L, "njin.%s: %lld is not a spline", fn, (long long)id);
    return it->second;
  };
  const auto set_points = [](lua_State *L, script_spline &sp, const sol::table &points, const char *fn) {
    sp.s3.points.clear();
    sp.s2.points.clear();
    for (usize i = 1; i <= points.size(); i++) {
      const sol::object p = points[i];
      if (sp.is3d)
        sp.s3.points.push_back(vec3_of(p, {}));
      else
        sp.s2.points.push_back(vec2_of(p, {}));
    }
    if ((sp.is3d ? sp.s3.points.size() : sp.s2.points.size()) < 2)
      fail(L, "njin.%s: a spline needs at least 2 points", fn);
  };
  // spline_create({points...}, {kind="catmull_rom"|"bezier", closed=, alpha=, steps=, owner=entity})
  // 2D or 3D by the first point (vec2 or vec3, or a table of 2 or 3 numbers).
  n["spline_create"] = [c, r, sweep, set_points](sol::this_state s, sol::table points,
                                                 sol::optional<sol::table> opts) {
    sweep();
    script_spline sp;
    const sol::object first = points[1];
    if (first.is<vec2>()) {
      sp.is3d = false;
    } else if (first.get_type() == sol::type::table && !first.is<vec3>()) {
      const sol::table p = first.as<sol::table>();
      sp.is3d = p["z"].valid() || p[3].valid();
    }
    set_points(s, sp, points, "spline_create");
    i32 steps = 64;
    if (opts) {
      const std::string kind = (*opts)["kind"].get_or(std::string("catmull_rom"));
      const spline_kind k = kind == "bezier" ? spline_bezier : spline_catmull_rom;
      const bool closed = flag(*opts, "closed", false);
      const f32 alpha = num(*opts, "alpha", 0.5f);
      sp.s3.kind = sp.s2.kind = k;
      sp.s3.closed = sp.s2.closed = closed;
      sp.s3.alpha = sp.s2.alpha = alpha;
      steps = (*opts)["steps"].get_or(64);
      if (sol::optional<lua_Integer> owner = (*opts)["owner"]) {
        sp.owner = script_entity(*owner);
        if (!world(*c).valid(sp.owner))
          fail(s, "njin.spline_create: owner %lld is not an entity", (long long)*owner);
      }
    }
    if (sp.is3d)
      spline_bake(sp.s3, steps);
    else
      spline_bake(sp.s2, steps);
    const u32 id = r->next_spline++;
    r->splines.emplace(id, std::move(sp));
    return (lua_Integer)id;
  };
  n["spline_set_points"] = [spline_at, set_points](sol::this_state s, lua_Integer id, sol::table points) {
    script_spline &sp = spline_at(s, id, "spline_set_points");
    set_points(s, sp, points, "spline_set_points");
    if (sp.is3d)
      spline_bake(sp.s3, sp.s3.steps > 0 ? sp.s3.steps : 64);
    else
      spline_bake(sp.s2, sp.s2.steps > 0 ? sp.s2.steps : 64);
  };
  n["spline_destroy"] = [r](lua_Integer id) { r->splines.erase((u32)id); };
  n["spline_valid"] = [r, sweep](lua_Integer id) {
    sweep();
    return r->splines.count((u32)id) != 0;
  };
  n["spline_length"] = [spline_at](sol::this_state s, lua_Integer id) {
    const script_spline &sp = spline_at(s, id, "spline_length");
    return sp.is3d ? spline_length(sp.s3) : spline_length(sp.s2);
  };
  const auto value = [r](const script_spline &sp, vec3 v3, vec2 v2) {
    return sp.is3d ? sol::make_object(r->lua, v3) : sol::make_object(r->lua, v2);
  };
  n["spline_point"] = [spline_at, value](sol::this_state s, lua_Integer id, f32 t) {
    const script_spline &sp = spline_at(s, id, "spline_point");
    return sp.is3d ? value(sp, spline_point(sp.s3, t), {}) : value(sp, {}, spline_point(sp.s2, t));
  };
  n["spline_point_at"] = [spline_at, value](sol::this_state s, lua_Integer id, f32 d) {
    const script_spline &sp = spline_at(s, id, "spline_point_at");
    return sp.is3d ? value(sp, spline_point_at(sp.s3, d), {}) : value(sp, {}, spline_point_at(sp.s2, d));
  };
  n["spline_tangent_at"] = [spline_at, value](sol::this_state s, lua_Integer id, f32 d) {
    const script_spline &sp = spline_at(s, id, "spline_tangent_at");
    return sp.is3d ? value(sp, spline_tangent_at(sp.s3, d), {}) : value(sp, {}, spline_tangent_at(sp.s2, d));
  };
  // distance, point = spline_nearest(id, p)
  n["spline_nearest"] = [spline_at, value](sol::this_state s, lua_Integer id, sol::object p) {
    const script_spline &sp = spline_at(s, id, "spline_nearest");
    if (sp.is3d) {
      vec3 at{};
      const f32 d = spline_nearest(sp.s3, vec3_of(p, {}), &at);
      return std::make_tuple(d, value(sp, at, {}));
    }
    vec2 at{};
    const f32 d = spline_nearest(sp.s2, vec2_of(p, {}), &at);
    return std::make_tuple(d, value(sp, {}, at));
  };
  // spline_follow(id, follower, dt): follower = {distance=, speed=, ["end"]="stop"|"loop"|"ping_pong"},
  // updated in place (distance, speed, finished); returns the new point.
  n["spline_follow"] = [spline_at, value](sol::this_state s, lua_Integer id, sol::table f, f32 dt) {
    const script_spline &sp = spline_at(s, id, "spline_follow");
    spline_follower fl{};
    fl.distance = num(f, "distance", 0.0f);
    fl.speed = num(f, "speed", 1.0f);
    fl.end = end_of(f["end"]);
    fl.finished = flag(f, "finished", false);
    const sol::object out = sp.is3d ? value(sp, spline_follow(sp.s3, fl, dt), {})
                                    : value(sp, {}, spline_follow(sp.s2, fl, dt));
    f["distance"] = fl.distance;
    f["speed"] = fl.speed;
    f["end"] = end_name(fl.end);
    f["finished"] = fl.finished;
    return out;
  };
  n["spline_draw_debug"] = [c, spline_at](sol::this_state s, lua_Integer id, sol::object color) {
    const script_spline &sp = spline_at(s, id, "spline_draw_debug");
    if (sp.is3d)
      spline_draw_debug(*c, sp.s3, color_of(color, {1.0f, 0.8f, 0.2f, 1.0f}));
    else
      spline_draw_debug(*c, sp.s2, color_of(color, {1.0f, 0.8f, 0.2f, 1.0f}));
  };
}
} // namespace njin

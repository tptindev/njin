#include "ui.h"
#include "njin2rl.h"
#include "njin_cfg.h"
#include "njin_ctx.h"
#include "njin_input.h"
#include "njin_ctx_impl.h"
#include "njin_draw.h"
#include "_tween.h"
#include "njin_bindings.h"
#include "njin_log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <raylib.h>
#include <string>
#include <vector>

namespace njin {
namespace {
// Holding a direction repeats it after this delay, at this rate.
constexpr f32 repeat_delay = 0.35f;
constexpr f32 repeat_rate = 0.08f;
// A stick past this counts as a pressed direction.
constexpr f32 stick_threshold = 0.5f;
constexpr i32 max_pads = 4;
// Room left above and below a panel that had to be shrunk (before `scale`), and
// how far it may shrink.
constexpr f32 panel_screen_margin = 6.0f;
constexpr f32 min_panel_fit = 0.5f;

enum widget_state { state_normal, state_focused, state_pressed, state_disabled };

u64 hash_id(u64 seed, const char *text) {
  u64 h = seed ^ 1469598103934665603ull;
  for (const char *c = text; *c != '\0'; c++) {
    h ^= (u8)*c;
    h *= 1099511628211ull;
  }
  return h == 0 ? 1 : h;
}

// The label as shown: everything before "##".
std::string shown(const char *label) {
  const char *hidden = std::strstr(label, "##");
  return hidden != nullptr ? std::string(label, (usize)(hidden - label)) : std::string(label);
}

bool inside(vec2 p, rect r) { return point_in_rect(p, r); }

const ui_skin &skin_for(const ui_look &look, widget_state s) {
  switch (s) {
  case state_focused: return look.focused;
  case state_pressed: return look.pressed;
  case state_disabled: return look.disabled;
  default: return look.normal;
  }
}

rgba text_for(const ui_look &look, widget_state s) {
  return s == state_disabled ? look.text_disabled
                             : (s == state_focused || s == state_pressed ? look.text_focused : look.text);
}

// --- drawing ---

void set_uniform_quiet(shader_slot &slot, const char *name, const void *value,
                       ShaderUniformDataType type) {
  auto it = slot.uniforms.find(name);
  if (it == slot.uniforms.end())
    it = slot.uniforms.emplace(name, GetShaderLocation(slot.shader, name)).first;
  if (it->second >= 0)
    SetShaderValue(slot.shader, it->second, value, type);
}

void draw_skin(context &ctx, const ui_cmd &c) {
  shader_slot *sh = shader_slot_of(ctx.shader, c.shader);
  if (sh != nullptr) {
    BeginShaderMode(sh->shader);
    shader_bind_textures(ctx, *sh);
    const f32 time = ctx.time.elapsed;
    f32 area[4] = {c.area.pos.x, c.area.pos.y, c.area.size.x, c.area.size.y};
    if (ctx.view.ui_window) {
      // The shader runs on window pixels in the smooth UI pass.
      const view_state &v = ctx.view;
      area[0] = v.offset.x + area[0] * v.scale;
      area[1] = v.offset.y + area[1] * v.scale;
      area[2] *= v.scale;
      area[3] *= v.scale;
    }
    set_uniform_quiet(*sh, "uiState", &c.state, SHADER_UNIFORM_FLOAT);
    set_uniform_quiet(*sh, "uiTime", &time, SHADER_UNIFORM_FLOAT);
    set_uniform_quiet(*sh, "uiRect", area, SHADER_UNIFORM_VEC4);
    set_uniform_quiet(*sh, "uiValue", &c.value, SHADER_UNIFORM_FLOAT);
  }
  const ui_skin &s = c.look;
  Color color{};
  to_raylib(s.color, color);
  const Rectangle dest{c.area.pos.x, c.area.pos.y, c.area.size.x, c.area.size.y};
  if (const texture_slot *tex = texture_slot_of(ctx.texture, s.texture)) {
    const bool whole = s.source.size.x <= 0.0f || s.source.size.y <= 0.0f;
    const Rectangle area = texture_area(*tex);
    const Rectangle src{area.x + (whole ? 0.0f : s.source.pos.x),
                        area.y + (whole ? 0.0f : s.source.pos.y),
                        whole ? area.width : s.source.size.x,
                        whole ? area.height : s.source.size.y};
    if (s.border > 0.0f) {
      const i32 b = (i32)s.border;
      const NPatchInfo patch{src, b, b, b, b, NPATCH_NINE_PATCH};
      DrawTextureNPatch(tex->texture, patch, dest, Vector2{0.0f, 0.0f}, 0.0f, color);
    } else {
      DrawTexturePro(tex->texture, src, dest, Vector2{0.0f, 0.0f}, 0.0f, color);
    }
  } else {
    if (s.color.a > 0.0f) {
      if (s.roundness > 0.0f)
        DrawRectangleRounded(dest, s.roundness, 8, color);
      else
        DrawRectangleRec(dest, color);
    }
    if (s.outline.a > 0.0f && s.outline_width > 0.0f) {
      Color line{};
      to_raylib(s.outline, line);
      DrawRectangleRoundedLinesEx(dest, s.roundness, 8, s.outline_width, line);
    }
  }
  if (sh != nullptr)
    EndShaderMode();
  // Text queued before this skin is under it (see view_state::text_layer). An
  // image skin has colours of its own, so it hides what is under it. A skin with
  // a shader draws whatever the shader decides, so it is left alone.
  if (sh == nullptr)
    view_text_occlude(ctx.view, c.area, s.color, texture_slot_of(ctx.texture, s.texture) != nullptr);
}

// A ring (circular progress bar). Our angles start at 12 o'clock and run
// clockwise; raylib's start at 3 o'clock, also clockwise on screen (y is down).
void draw_ring(const ui_cmd &c) {
  const vec2 center = rect_center(c.area);
  const f32 outer = std::min(c.area.size.x, c.area.size.y) * 0.5f;
  const f32 band = std::clamp(c.thickness, 1.0f, outer);
  const f32 inner = outer - band;
  if (outer < 1.0f)
    return;
  const Vector2 mid{center.x, center.y};
  const i32 full_segments = std::clamp((i32)(outer * 0.8f), 32, 160);
  const auto arc = [&](f32 from, f32 to, Color color) {
    const i32 segments = std::max(2, (i32)std::ceil((f32)full_segments * (to - from) / 360.0f));
    DrawRing(mid, inner, outer, from, to, segments, color);
  };
  Color track{}, fill{};
  to_raylib(c.track, track);
  to_raylib(c.color, fill);
  if (c.show_track && c.track.a > 0.0f)
    arc(0.0f, 360.0f, track);
  const f32 v = std::clamp(c.value, 0.0f, 1.0f);
  if (v <= 0.0f || c.color.a <= 0.0f)
    return;
  const f32 sweep = 360.0f * v;
  const f32 start = c.start - 90.0f;
  const f32 from = c.clockwise ? start : start - sweep;
  arc(from, from + sweep, fill);
  if (c.round_caps && v < 1.0f) {
    const f32 radius = band * 0.5f;
    const f32 path = inner + radius;
    for (const f32 degrees : {start, c.clockwise ? start + sweep : start - sweep}) {
      const f32 a = degrees * (3.14159265f / 180.0f);
      DrawCircleV(Vector2{center.x + std::cos(a) * path, center.y + std::sin(a) * path}, radius, fill);
    }
  }
}

void flush(context &ctx, const ui_state &ui, const std::vector<ui_cmd> &cmds) {
  for (const ui_cmd &c : cmds) {
    switch (c.kind) {
    case ui_cmd::skin:
      draw_skin(ctx, c);
      break;
    case ui_cmd::ring:
      draw_ring(c);
      break;
    case ui_cmd::text:
      draw_text(ctx, c.str.c_str(), c.area.pos, c.size, c.color, ui.style.font);
      break;
    case ui_cmd::image:
      texture_draw_ex(ctx, c.texture,
                      texture_draw_desc{.pos = c.area.pos,
                                        .source = c.source,
                                        .scale = c.scale,
                                        .tint = c.color});
      break;
    }
  }
}

// --- input ---

bool any_pad_pressed(const context &ctx, gamepad_button b) {
  for (i32 p = 0; p < max_pads; p++)
    if (pad_available(ctx, p) && pad_pressed(ctx, p, b))
      return true;
  return false;
}

bool any_pad_held(const context &ctx, gamepad_button b) {
  for (i32 p = 0; p < max_pads; p++)
    if (pad_available(ctx, p) && pad_held(ctx, p, b))
      return true;
  return false;
}

// Direction held on any stick of any pad: 1 up, 2 down, 3 left, 4 right.
i32 stick_dir(const context &ctx) {
  for (i32 p = 0; p < max_pads; p++) {
    if (!pad_available(ctx, p))
      continue;
    const f32 x = pad_axis(ctx, p, pad_axis_left_x);
    const f32 y = pad_axis(ctx, p, pad_axis_left_y);
    if (std::abs(y) >= std::abs(x)) {
      if (y < -stick_threshold) return 1;
      if (y > stick_threshold) return 2;
    } else {
      if (x < -stick_threshold) return 3;
      if (x > stick_threshold) return 4;
    }
  }
  return 0;
}

// Picks the widget nearest `from` in a direction, or wraps for up/down.
const ui_widget_rec *next_in(const std::vector<ui_widget_rec> &list, const ui_widget_rec &from,
                             i32 dir) {
  const vec2 c = rect_center(from.area);
  const ui_widget_rec *best = nullptr;
  f32 best_score = 0.0f;
  for (const ui_widget_rec &w : list) {
    if (w.id == from.id)
      continue;
    const vec2 d = rect_center(w.area) - c;
    f32 along = 0.0f, across = 0.0f;
    switch (dir) {
    case 1: along = -d.y; across = d.x; break;
    case 2: along = d.y; across = d.x; break;
    case 3: along = -d.x; across = d.y; break;
    default: along = d.x; across = d.y; break;
    }
    if (along <= 0.5f)
      continue;
    // Sideways only within the same row.
    if (dir >= 3 && std::abs(across) > from.area.size.y * 0.5f)
      continue;
    const f32 score = along + std::abs(across) * 2.0f;
    if (best == nullptr || score < best_score) {
      best = &w;
      best_score = score;
    }
  }
  if (best != nullptr || dir >= 3)
    return best;
  // Wrap: up from the top goes to the bottom, and back.
  for (const ui_widget_rec &w : list) {
    const f32 y = rect_center(w.area).y;
    if (best == nullptr || (dir == 1 ? y > rect_center(best->area).y : y < rect_center(best->area).y))
      best = &w;
  }
  return best != nullptr && best->id != from.id ? best : nullptr;
}

void play(context &ctx, sound_handle s) {
  if (s.id != 0)
    sound_play_once(ctx, s);
}

void frame_begin(context &ctx) {
  ui_state &ui = ctx.ui;
  ui.last = std::move(ui.current);
  ui.current.clear();
  ui.last_panels = std::move(ui.panels);
  ui.panels.clear();
  ui.back_reported = false;
  const u64 prev_modal = ui.modal_last;
  ui.modal_last = ui.modal;
  ui.modal = 0;
  // While a popup is up, only its widgets can be reached.
  if (ui.modal_last != 0) {
    const u64 keep = ui.modal_last;
    ui.last.erase(std::remove_if(ui.last.begin(), ui.last.end(),
                                 [keep](const ui_widget_rec &w) { return w.panel != keep; }),
                  ui.last.end());
  } else if (prev_modal != 0) {
    ui.focus = ui.saved_focus; // the popup is gone: back to where the player was
  }
  ui.navigable_last = ui.navigable_now;
  ui.navigable_now = false;
  const bool active = ui.navigable_last;
  // A keybind that was not drawn last frame (its menu closed) stops waiting.
  if (ui.listening != 0 &&
      std::none_of(ui.last.begin(), ui.last.end(),
                   [&](const ui_widget_rec &w) { return w.id == ui.listening; }))
    ui.listening = 0;

  // Raw input, read before anything is consumed.
  const f32 dt = ctx.time.dt_real;
  const vec2 mouse = mouse_pos(ctx);
  ui.mouse_moved = mouse != ui.mouse;
  ui.mouse = mouse;
  ui.mouse_pressed = mouse_pressed(ctx, mouse_left);
  ui.mouse_held = mouse_held(ctx, mouse_left);
  ui.mouse_released = mouse_released(ctx, mouse_left);
  if (!ui.mouse_held && !ui.mouse_released)
    ui.pressed = 0;
  ui.accept = key_pressed(ctx, key_enter) || key_pressed(ctx, key_space) ||
              any_pad_pressed(ctx, pad_face_down);
  ui.accept_held = ui.accept || key_held(ctx, key_enter) || key_held(ctx, key_space) ||
                   any_pad_held(ctx, pad_face_down);
  ui.back = key_pressed(ctx, key_escape) || key_pressed(ctx, key_backspace) ||
            any_pad_pressed(ctx, pad_face_right);

  // Directions, with repeat while held. "Down" is pressed-or-held: key_held
  // alone is false on the frame a key goes down.
  const auto down = [&](key_code k, gamepad_button b) {
    return key_pressed(ctx, k) || key_held(ctx, k) || any_pad_pressed(ctx, b) ||
           any_pad_held(ctx, b);
  };
  i32 held = 0;
  if (down(key_up, pad_dpad_up)) held = 1;
  else if (down(key_down, pad_dpad_down)) held = 2;
  else if (down(key_left, pad_dpad_left)) held = 3;
  else if (down(key_right, pad_dpad_right)) held = 4;
  else held = stick_dir(ctx);
  i32 fire = 0;
  if (held != ui.repeat_dir) {
    ui.repeat_dir = held;
    ui.repeat_timer = repeat_delay;
    fire = held;
  } else if (held != 0) {
    ui.repeat_timer -= dt;
    if (ui.repeat_timer <= 0.0f) {
      ui.repeat_timer += repeat_rate;
      fire = held;
    }
  }
  ui.up = fire == 1;
  ui.down = fire == 2;
  ui.left = fire == 3;
  ui.right = fire == 4;
  ui.adjust = 0;
  if (ui.listening != 0) {
    // A keybind is waiting for a press: that press is not navigation.
    ui.up = ui.down = ui.left = ui.right = false;
    ui.accept = ui.accept_held = ui.back = false;
    ui.mouse_pressed = ui.mouse_released = false;
  }

  ui.mouse_over = std::any_of(ui.last_panels.begin(), ui.last_panels.end(),
                              [&](const rect &p) { return inside(mouse, p); });
  if (ui.mouse_over)
    mouse_consume(ctx, mouse_left);
  if (!active) {
    // No panel takes the keys (none at all, or only HUD panels): they stay the game's.
    ui.up = ui.down = ui.left = ui.right = false;
    ui.accept = ui.accept_held = ui.back = false;
    return;
  }

  // The UI owns these keys while it is on screen.
  for (const key_code k : {key_up, key_down, key_left, key_right, key_enter, key_space,
                           key_escape, key_backspace})
    key_consume(ctx, k);

  // Keep the focus on something that exists.
  const auto find = [&](u64 id) -> const ui_widget_rec * {
    for (const ui_widget_rec &w : ui.last)
      if (w.id == id)
        return &w;
    return nullptr;
  };
  const ui_widget_rec *focused = find(ui.focus);
  if (focused == nullptr && !ui.last.empty()) {
    ui.focus = ui.last.front().id;
    focused = &ui.last.front();
  }
  if (focused == nullptr)
    return;
  if (focused->adjustable && (ui.left || ui.right)) {
    ui.adjust = ui.left ? -1 : 1;
    return;
  }
  const i32 dir = ui.up ? 1 : ui.down ? 2 : ui.left ? 3 : ui.right ? 4 : 0;
  if (dir == 0)
    return;
  if (const ui_widget_rec *next = next_in(ui.last, *focused, dir)) {
    ui.focus = next->id;
    play(ctx, ui.style.sound_move);
  }
}

void setup(context &ctx) { ecs_register(ctx, phase_pre_update, frame_begin, "frame_begin"); }

// --- layout ---

f32 sc(const ui_state &ui, f32 v) { return v * ui.style.scale * ui.fit; }
f32 font_px(const ui_state &ui) { return sc(ui, ui.style.font_size); }

// Takes the next slot of height `h` in the panel (a column of the current
// row, or a full-width line).
rect place(ui_state &ui, f32 h) {
  const f32 pad = sc(ui, ui.style.padding);
  const f32 gap = sc(ui, ui.style.spacing);
  const f32 inner = ui.panel.size.x - 2.0f * pad;
  if (ui.row_cols > 0) {
    const f32 w = (inner - gap * (f32)(ui.row_cols - 1)) / (f32)ui.row_cols;
    const rect r{{ui.panel.pos.x + pad + (w + gap) * (f32)ui.row_index, ui.row_y}, {w, h}};
    if (++ui.row_index >= ui.row_cols) {
      ui.row_cols = 0;
      ui.cursor = ui.row_y + h + gap;
    }
    ui.last_rect = r;
    return r;
  }
  const rect r{{ui.panel.pos.x + pad, ui.cursor}, {inner, h}};
  ui.cursor += h + gap;
  ui.last_rect = r;
  return r;
}

void push_skin(ui_state &ui, const ui_look &look, widget_state s, rect area, f32 value = 0.0f) {
  ui_cmd c{};
  c.kind = ui_cmd::skin;
  c.area = area;
  c.look = skin_for(look, s);
  c.shader = look.shader;
  c.state = (f32)s;
  c.value = value;
  ui.cmds.push_back(std::move(c));
}

void push_text(context &ctx, ui_state &ui, const std::string &text, vec2 pos, rgba color,
               f32 size = 0.0f) {
  ui_cmd c{};
  c.kind = ui_cmd::text;
  c.str = text;
  c.size = size > 0.0f ? size : font_px(ui);
  c.area.pos = pos;
  c.color = color;
  (void)ctx;
  ui.cmds.push_back(std::move(c));
}

vec2 measure(const context &ctx, const ui_state &ui, const std::string &text, f32 size = 0.0f) {
  return text_measure(ctx, text.c_str(), size > 0.0f ? size : font_px(ui), ui.style.font);
}

// Text centred in `area`.
void text_center(context &ctx, ui_state &ui, const std::string &text, rect area, rgba color) {
  const vec2 m = measure(ctx, ui, text);
  push_text(ctx, ui, text, rect_center(area) - m * 0.5f, color);
}

// Text at the left of `area`, vertically centred.
void text_left(context &ctx, ui_state &ui, const std::string &text, rect area, rgba color) {
  const vec2 m = measure(ctx, ui, text);
  push_text(ctx, ui, text, {area.pos.x + sc(ui, 14.0f), rect_center(area).y - m.y * 0.5f}, color);
}

// Text at the right of `area`, vertically centred.
void text_right(context &ctx, ui_state &ui, const std::string &text, rect area, rgba color) {
  const vec2 m = measure(ctx, ui, text);
  push_text(ctx, ui, text,
            {area.pos.x + area.size.x - sc(ui, 14.0f) - m.x, rect_center(area).y - m.y * 0.5f}, color);
}

struct interaction {
  u64 id = 0;
  rect area{};
  widget_state state = state_normal;
  bool clicked = false; // mouse click released on it, or accept while focused
  bool hover = false;
};

// Places a focusable widget and works out its state from the input.
interaction interact(context &ctx, const char *label, bool enabled, bool adjustable) {
  ui_state &ui = ctx.ui;
  interaction it;
  it.id = hash_id(ui.panel_id, label);
  it.area = place(ui, sc(ui, ui.style.widget_height));
  if (!enabled) {
    it.state = state_disabled;
    return it;
  }
  if (!ui.panel_navigable) {
    // HUD panel: mouse only, never focused, not reachable by the keys.
    it.hover = ui.modal_last == 0 && inside(ui.mouse, it.area);
    if (it.hover && ui.mouse_pressed)
      ui.pressed = it.id;
    it.clicked = ui.pressed == it.id && ui.mouse_released && it.hover;
    const bool held = ui.pressed == it.id && ui.mouse_held;
    it.state = held ? state_pressed : it.hover ? state_focused : state_normal;
    return it;
  }
  ui.current.push_back({it.id, it.area, adjustable, ui.panel_id});
  // Behind a popup: listed (so focus can come back to it) but inert.
  if (ui.modal_last != 0 && ui.panel_id != ui.modal_last)
    return it;
  it.hover = inside(ui.mouse, it.area);
  if (it.hover && ui.mouse_moved && ui.focus != it.id) {
    ui.focus = it.id;
    play(ctx, ui.style.sound_move);
  }
  if (it.hover && ui.mouse_pressed) {
    ui.pressed = it.id;
    ui.focus = it.id;
  }
  const bool focused = ui.focus == it.id;
  it.clicked = (ui.pressed == it.id && ui.mouse_released && it.hover) || (focused && ui.accept);
  const bool held = (ui.pressed == it.id && ui.mouse_held) || (focused && ui.accept_held);
  it.state = held ? state_pressed : focused ? state_focused : state_normal;
  return it;
}

void require_panel(const ui_state &ui, const char *what) {
  if (!ui.in_panel)
    NJIN_WARN("%s called outside ui_begin/ui_end", what);
}
} // namespace

mod_desc ui_module() { return mod_desc{.name = "njin.ui", .setup = setup}; }

ui_style ui_default_style() {
  ui_style s{};
  const rgba accent{0.26f, 0.56f, 0.98f, 1.0f};
  s.panel.normal = {.color = {0.07f, 0.08f, 0.11f, 0.88f}, .roundness = 0.06f,
                    .outline = {1.0f, 1.0f, 1.0f, 0.08f}, .outline_width = 1.0f};
  s.panel.text = {0.92f, 0.94f, 1.0f, 1.0f};
  s.label.text = {0.78f, 0.81f, 0.88f, 1.0f};
  s.label.text_focused = s.label.text;
  s.button.normal = {.color = {0.16f, 0.18f, 0.24f, 1.0f}, .roundness = 0.3f};
  s.button.focused = {.color = {0.21f, 0.25f, 0.34f, 1.0f}, .roundness = 0.3f,
                      .outline = accent, .outline_width = 2.0f};
  s.button.pressed = {.color = {0.13f, 0.30f, 0.60f, 1.0f}, .roundness = 0.3f,
                      .outline = accent, .outline_width = 2.0f};
  s.button.disabled = {.color = {0.12f, 0.13f, 0.16f, 1.0f}, .roundness = 0.3f};
  s.button.text = {0.86f, 0.89f, 0.95f, 1.0f};
  s.button.text_focused = {1.0f, 1.0f, 1.0f, 1.0f};
  s.button.text_disabled = {0.42f, 0.44f, 0.50f, 1.0f};
  const ui_skin track{.color = {0.25f, 0.28f, 0.36f, 1.0f}, .roundness = 1.0f};
  s.track.normal = s.track.focused = s.track.pressed = s.track.disabled = track;
  s.track.text = s.track.text_focused = {1.0f, 1.0f, 1.0f, 1.0f};
  const ui_skin fill{.color = accent, .roundness = 1.0f};
  s.fill.normal = s.fill.focused = s.fill.pressed = fill;
  s.fill.disabled = {.color = {0.3f, 0.32f, 0.38f, 1.0f}, .roundness = 1.0f};
  s.knob.normal = {.color = {0.85f, 0.88f, 0.95f, 1.0f}, .roundness = 1.0f};
  s.knob.focused = s.knob.pressed = {.color = {1.0f, 1.0f, 1.0f, 1.0f}, .roundness = 1.0f,
                                     .outline = accent, .outline_width = 2.0f};
  s.knob.disabled = {.color = {0.4f, 0.42f, 0.48f, 1.0f}, .roundness = 1.0f};
  s.toast.normal = {.color = {0.09f, 0.10f, 0.14f, 0.95f}, .roundness = 0.25f,
                    .outline = {1.0f, 1.0f, 1.0f, 0.10f}, .outline_width = 1.0f};
  s.toast.text = {0.93f, 0.95f, 1.0f, 1.0f};
  return s;
}

void ui_style_set(context &ctx, const ui_style &style) { ctx.ui.style = style; }
ui_style ui_style_get(const context &ctx) { return ctx.ui.style; }

void ui_begin(context &ctx, const ui_panel_desc &desc) {
  ui_state &ui = ctx.ui;
  if (ui.in_panel) {
    NJIN_WARN("ui_begin: panel '%s' opened inside another; close it with ui_end first",
              desc.id != nullptr ? desc.id : "");
    ui_end(ctx);
  }
  ui.in_panel = true;
  ui.panel_id = hash_id(0, desc.id != nullptr ? desc.id : "panel");
  ui.panel_title = desc.title;
  ui.panel_background = desc.background;
  ui.panel_navigable = desc.navigable;
  ui.cmds.clear();
  ui.row_cols = 0;
  const vec2 screen = screen_size(ctx);
  // A panel taller than the screen would run off it, top and bottom: shrink
  // its sizes (text included) by the factor that makes it fit, down to half.
  // The height it would have unshrunk is known from the frame before.
  ui.fit = 1.0f;
  const auto nat = ui.natural.find(ui.panel_id);
  if (nat != ui.natural.end() && nat->second > 0.0f) {
    const f32 room = screen.y - 2.0f * sc(ui, panel_screen_margin);
    ui.fit = std::clamp(room / nat->second, min_panel_fit, 1.0f);
  }
  const f32 width = sc(ui, desc.width > 0.0f ? desc.width : ui.style.width);
  // The height is last frame's: the panel is placed before its content is
  // known. It settles on the first frame a panel is shown.
  const auto h = ui.heights.find(ui.panel_id);
  f32 height = h != ui.heights.end() ? h->second : 0.0f;
  if (nat != ui.natural.end())
    height = nat->second * ui.fit;
  const vec2 size{width, height};
  ui.panel = rect{screen * desc.anchor - size * desc.pivot + desc.offset, size};
  ui.cursor = ui.panel.pos.y + sc(ui, ui.style.padding);
  if (desc.title != nullptr) {
    const f32 title_size = font_px(ui) * 1.3f;
    const vec2 m = measure(ctx, ui, desc.title, title_size);
    push_text(ctx, ui, desc.title, {ui.panel.pos.x + (width - m.x) * 0.5f, ui.cursor},
              ui.style.panel.text, title_size);
    ui.cursor += m.y + sc(ui, ui.style.spacing) * 1.6f;
  }
}

void ui_end(context &ctx) {
  ui_state &ui = ctx.ui;
  if (!ui.in_panel) {
    NJIN_WARN("ui_end without ui_begin");
    return;
  }
  ui.in_panel = false;
  if (ui.row_cols > 0) { // a row left unfinished
    ui.cursor = ui.row_y + sc(ui, ui.style.widget_height) + sc(ui, ui.style.spacing);
    ui.row_cols = 0;
  }
  const f32 height = ui.cursor - sc(ui, ui.style.spacing) + sc(ui, ui.style.padding) - ui.panel.pos.y;
  ui.heights[ui.panel_id] = height;
  ui.natural[ui.panel_id] = height / ui.fit;
  ui.fit = 1.0f;
  rect bg = ui.panel;
  bg.size.y = height;
  ui.panels.push_back(bg);
  if (ui.panel_navigable)
    ui.navigable_now = true;
  if (ui.panel_background) {
    ui_cmd c{};
    c.kind = ui_cmd::skin;
    c.area = bg;
    c.look = ui.style.panel.normal;
    c.shader = ui.style.panel.shader;
    flush(ctx, ui, {c});
  }
  flush(ctx, ui, ui.cmds);
  ui.cmds.clear();
}

void ui_row(context &ctx, i32 columns) {
  ui_state &ui = ctx.ui;
  require_panel(ui, "ui_row");
  if (columns <= 0)
    return;
  ui.row_cols = columns;
  ui.row_index = 0;
  ui.row_y = ui.cursor;
}

void ui_label(context &ctx, const char *text) {
  ui_state &ui = ctx.ui;
  require_panel(ui, "ui_label");
  if (text == nullptr)
    return;
  const vec2 m = measure(ctx, ui, text);
  const rect r = place(ui, m.y);
  push_text(ctx, ui, text, r.pos, ui.style.label.text);
}

void ui_space(context &ctx, f32 height) {
  ui_state &ui = ctx.ui;
  require_panel(ui, "ui_space");
  const f32 pad = sc(ui, ui.style.padding);
  ui.last_rect = rect{{ui.panel.pos.x + pad, ui.cursor}, {ui.panel.size.x - 2.0f * pad, sc(ui, height)}};
  ui.cursor += sc(ui, height);
}

bool ui_button(context &ctx, const char *label, bool enabled) {
  ui_state &ui = ctx.ui;
  require_panel(ui, "ui_button");
  if (label == nullptr)
    return false;
  const interaction it = interact(ctx, label, enabled, false);
  push_skin(ui, ui.style.button, it.state, it.area);
  text_center(ctx, ui, shown(label), it.area, text_for(ui.style.button, it.state));
  if (it.clicked)
    play(ctx, ui.style.sound_accept);
  return it.clicked;
}

bool ui_toggle(context &ctx, const char *label, bool &value) {
  ui_state &ui = ctx.ui;
  require_panel(ui, "ui_toggle");
  if (label == nullptr)
    return false;
  const interaction it = interact(ctx, label, true, false);
  bool changed = false;
  if (it.clicked) {
    value = !value;
    changed = true;
    play(ctx, ui.style.sound_accept);
  }
  push_skin(ui, ui.style.button, it.state, it.area);
  text_left(ctx, ui, shown(label), it.area, text_for(ui.style.button, it.state));
  const f32 box = it.area.size.y * 0.55f;
  const rect b{{it.area.pos.x + it.area.size.x - sc(ui, 14.0f) - box * 1.8f,
                rect_center(it.area).y - box * 0.5f},
               {box * 1.8f, box}};
  push_skin(ui, ui.style.track, it.state, b, value ? 1.0f : 0.0f);
  // A switch: the knob sits left when off, right when on.
  const f32 k = box - sc(ui, 6.0f);
  const rect knob{{value ? b.pos.x + b.size.x - k - sc(ui, 3.0f) : b.pos.x + sc(ui, 3.0f),
                   b.pos.y + sc(ui, 3.0f)},
                  {k, k}};
  if (value)
    push_skin(ui, ui.style.fill, it.state, b, 1.0f);
  push_skin(ui, ui.style.knob, it.state, knob, value ? 1.0f : 0.0f);
  return changed;
}

bool ui_slider(context &ctx, const char *label, f32 &value, f32 min, f32 max, f32 step,
               bool percent) {
  ui_state &ui = ctx.ui;
  require_panel(ui, "ui_slider");
  if (label == nullptr || max <= min)
    return false;
  const interaction it = interact(ctx, label, true, true);
  const f32 span = max - min;
  const f32 keystep = step > 0.0f ? step : span / 20.0f;
  const f32 before = value;

  const f32 pad = sc(ui, 14.0f);
  const f32 value_w = measure(ctx, ui, "0000").x;
  const f32 x0 = it.area.pos.x + it.area.size.x * 0.46f;
  const f32 x1 = it.area.pos.x + it.area.size.x - pad - value_w - pad;
  if (ui.focus == it.id && ui.adjust != 0)
    value += keystep * (f32)ui.adjust;
  if (ui.pressed == it.id && ui.mouse_held && x1 > x0) {
    const f32 t = clamp((ui.mouse.x - x0) / (x1 - x0), 0.0f, 1.0f);
    value = min + t * span;
    if (step > 0.0f)
      value = min + std::round((value - min) / step) * step;
  }
  value = clamp(value, min, max);
  const bool changed = value != before;
  if (changed && ui.adjust != 0)
    play(ctx, ui.style.sound_move);

  const f32 t = (value - min) / span;
  push_skin(ui, ui.style.button, it.state, it.area, t);
  text_left(ctx, ui, shown(label), it.area, text_for(ui.style.button, it.state));
  const f32 th = sc(ui, 8.0f);
  const rect track{{x0, rect_center(it.area).y - th * 0.5f}, {x1 - x0, th}};
  push_skin(ui, ui.style.track, it.state, track, t);
  if (t > 0.0f)
    push_skin(ui, ui.style.fill, it.state, rect{track.pos, {track.size.x * t, th}}, t);
  const f32 k = it.area.size.y * 0.42f;
  push_skin(ui, ui.style.knob, it.state,
            rect{{x0 + (x1 - x0) * t - k * 0.5f, rect_center(it.area).y - k * 0.5f}, {k, k}}, t);
  char text[32];
  if (percent)
    std::snprintf(text, sizeof text, "%d%%", (i32)std::lround(t * 100.0f));
  else if (step >= 1.0f && std::floor(step) == step)
    std::snprintf(text, sizeof text, "%d", (i32)std::lround(value));
  else
    std::snprintf(text, sizeof text, "%.2f", value);
  text_right(ctx, ui, text, it.area, text_for(ui.style.button, it.state));
  return changed;
}

namespace {
template <typename F>
bool ui_choice_impl(context &ctx, const char *label, i32 &index, i32 count, F &&get_option) {
  ui_state &ui = ctx.ui;
  require_panel(ui, "ui_choice");
  if (label == nullptr || count == 0)
    return false;
  const interaction it = interact(ctx, label, true, true);
  const i32 before = index;
  i32 delta = 0;
  if (ui.focus == it.id && ui.adjust != 0)
    delta = ui.adjust;
  else if (it.clicked) {
    // Mouse: the left part of the value steps back, the rest forward. Keys: forward.
    const bool mouse_click = ui.pressed == it.id && ui.mouse_released;
    delta = mouse_click && ui.mouse.x < it.area.pos.x + it.area.size.x * 0.72f ? -1 : 1;
  }
  index = ((index + delta) % count + count) % count;
  const bool changed = index != before;
  if (changed)
    play(ctx, ui.style.sound_accept);

  push_skin(ui, ui.style.button, it.state, it.area);
  const rgba color = text_for(ui.style.button, it.state);
  text_left(ctx, ui, shown(label), it.area, color);
  const std::string value = std::string("<  ") + get_option(index) + "  >";
  text_right(ctx, ui, value, it.area, color);
  return changed;
}
} // namespace

bool ui_choice(context &ctx, const char *label, i32 &index,
               std::initializer_list<const char *> options) {
  return ui_choice_impl(ctx, label, index, (i32)options.size(),
                        [&](i32 i) { return *(options.begin() + i); });
}

bool ui_choice(context &ctx, const char *label, i32 &index,
               std::span<const std::string> options) {
  return ui_choice_impl(ctx, label, index, (i32)options.size(),
                        [&](i32 i) { return options[(size_t)i].c_str(); });
}

void ui_progress(context &ctx, f32 value, const char *text) {
  ui_state &ui = ctx.ui;
  require_panel(ui, "ui_progress");
  const f32 t = clamp(value, 0.0f, 1.0f);
  const rect r = place(ui, sc(ui, ui.style.widget_height) * 0.6f);
  push_skin(ui, ui.style.track, state_normal, r, t);
  if (t > 0.0f)
    push_skin(ui, ui.style.fill, state_normal, rect{r.pos, {r.size.x * t, r.size.y}}, t);
  if (text != nullptr)
    text_center(ctx, ui, text, r, ui.style.track.text);
}

void ui_progress_circle(context &ctx, const ui_circle_desc &d) {
  ui_state &ui = ctx.ui;
  require_panel(ui, "ui_progress_circle");
  const f32 diameter = std::max(sc(ui, d.diameter), 1.0f);
  const rect slot = place(ui, diameter);
  // In a narrow column the circle shrinks to fit; the row keeps its height.
  const f32 size = std::min(diameter, slot.size.x);
  const rect box{{rect_center(slot).x - size * 0.5f, slot.pos.y + (diameter - size) * 0.5f}, {size, size}};
  const auto pick = [](rgba own, rgba style) { return own.a > 0.0f ? own : style; };
  ui_cmd c{};
  c.kind = ui_cmd::ring;
  c.area = box;
  c.value = d.value;
  c.color = pick(d.fill, ui.style.fill.normal.color);
  c.track = pick(d.track, ui.style.track.normal.color);
  c.thickness = sc(ui, d.thickness);
  c.start = d.start_angle;
  c.clockwise = d.clockwise;
  c.round_caps = d.round_caps;
  c.show_track = d.show_track;
  ui.cmds.push_back(std::move(c));

  std::string text;
  if (d.text != nullptr)
    text = d.text;
  else if (d.percent)
    text = std::to_string((i32)std::lround(clamp(d.value, 0.0f, 1.0f) * 100.0f)) + "%";
  if (!text.empty()) {
    const vec2 m = measure(ctx, ui, text);
    push_text(ctx, ui, text, rect_center(box) - m * 0.5f, pick(d.text_color, ui.style.panel.text));
  }
}

void ui_image(context &ctx, texture_handle texture, vec2 size, rect source) {
  ui_state &ui = ctx.ui;
  require_panel(ui, "ui_image");
  const vec2 s = size * ui.style.scale;
  const rect slot = place(ui, s.y);
  ui_cmd c{};
  c.kind = ui_cmd::image;
  c.texture = texture;
  c.color = {1.0f, 1.0f, 1.0f, 1.0f};
  c.area = rect{{rect_center(slot).x - s.x * 0.5f, slot.pos.y}, s};
  // texture_draw_ex scales from the source size; express the target size.
  const texture_slot *tex = texture_slot_of(ctx.texture, texture);
  const vec2 src = source.size.x > 0.0f && source.size.y > 0.0f
                       ? source.size
                       : (tex != nullptr ? vec2{texture_area(*tex).width, texture_area(*tex).height} : s);
  c.source = source;
  c.scale = {src.x > 0.0f ? s.x / src.x : 1.0f, src.y > 0.0f ? s.y / src.y : 1.0f};
  ui.cmds.push_back(std::move(c));
}

void ui_draw_look(context &ctx, const ui_look &look, i32 state, rect area, f32 value) {
  ui_cmd c{};
  c.kind = ui_cmd::skin;
  c.area = area;
  c.look = skin_for(look, (widget_state)std::clamp(state, 0, 3));
  c.shader = look.shader;
  c.state = (f32)state;
  c.value = value;
  flush(ctx, ctx.ui, {c});
}

bool ui_keybind(context &ctx, const char *label, action_handle action, bool pad) {
  ui_state &ui = ctx.ui;
  require_panel(ui, "ui_keybind");
  if (label == nullptr)
    return false;
  const interaction it = interact(ctx, label, true, false);
  bool changed = false;
  if (ui.listening == it.id) {
    input_source s{};
    if (input_any_pressed(ctx, s)) {
      const bool cancel = s.kind == input_source::key && s.code == key_escape;
      const bool fits = pad ? s.kind == input_source::pad : s.kind != input_source::pad;
      if (cancel) {
        ui.listening = 0;
        play(ctx, ui.style.sound_back);
      } else if (fits) {
        action_rebind(ctx, action, s);
        ui.listening = 0;
        changed = true;
        play(ctx, ui.style.sound_accept);
      }
    }
  } else if (it.clicked && ui.listening == 0) {
    ui.listening = it.id;
    ui.listen_kind = pad ? 1 : 0;
    play(ctx, ui.style.sound_accept);
  }
  const bool waiting = ui.listening == it.id;
  const widget_state state = waiting ? state_pressed : it.state;
  push_skin(ui, ui.style.button, state, it.area);
  const rgba color = text_for(ui.style.button, state);
  text_left(ctx, ui, shown(label), it.area, color);
  std::string value = "...";
  if (!waiting) {
    value.clear();
    for (const input_source &s : action_sources(ctx, action)) {
      if ((s.kind == input_source::pad) != pad)
        continue;
      if (!value.empty())
        value += ", ";
      value += input_source_name(s);
    }
    if (value.empty())
      value = "-";
  }
  text_right(ctx, ui, value, it.area, color);
  return changed;
}

bool ui_keybind_listening(const context &ctx) { return ctx.ui.listening != 0; }

bool ui_back(context &ctx) {
  ui_state &ui = ctx.ui;
  if (!ui.back || ui.last_panels.empty())
    return false;
  // With a popup up, back belongs to the popup alone.
  if (ui.modal_last != 0 && !(ui.in_panel && ui.panel_id == ui.modal_last))
    return false;
  if (!ui.back_reported) {
    ui.back_reported = true;
    play(ctx, ui.style.sound_back);
  }
  return true;
}

void ui_focus(context &ctx, const char *label) {
  ui_state &ui = ctx.ui;
  if (label != nullptr)
    ui.focus = hash_id(ui.panel_id, label);
}

bool ui_active(const context &ctx) { return ctx.ui.navigable_last; }

rect ui_last_rect(const context &ctx) { return ctx.ui.last_rect; }

bool ui_mouse_over(const context &ctx) { return ctx.ui.mouse_over; }

// --- popup ---

namespace {
// Greedy word wrap of `text` (which may hold newlines) to `max_w` pixels.
// Breaks only at spaces, so UTF-8 sequences are never split; a word wider than
// the line stays whole.
std::vector<std::string> wrap_lines(const context &ctx, const ui_state &ui, const char *text,
                                    f32 max_w, f32 size) {
  return text_wrap(ctx, text, size, max_w, ui.style.font);
}
} // namespace

void ui_popup_begin(context &ctx, const ui_popup_desc &desc) {
  ui_state &ui = ctx.ui;
  const char *name = desc.id != nullptr ? desc.id : "popup";
  const u64 id = hash_id(0, name);
  // First frame of this popup: remember where the focus was, to give it back.
  if (ui.modal_last != id && ui.modal != id)
    ui.saved_focus = ui.focus;
  ui.modal = id;
  draw_rect(ctx, rect{{0.0f, 0.0f}, screen_size(ctx)}, ui.style.dim);
  ui_begin(ctx, ui_panel_desc{.id = name, .title = desc.title, .width = desc.width});
}

void ui_popup_end(context &ctx) { ui_end(ctx); }

i32 ui_popup(context &ctx, const ui_popup_desc &desc, bool &open) {
  if (!open)
    return -1;
  ui_state &ui = ctx.ui;
  const u64 id = hash_id(0, desc.id != nullptr ? desc.id : "popup");
  const bool first = ui.modal_last != id;
  ui_popup_begin(ctx, desc);

  if (desc.message != nullptr) {
    const f32 inner = sc(ui, desc.width > 0.0f ? desc.width : ui.style.width) -
                      2.0f * sc(ui, ui.style.padding);
    for (const std::string &line : wrap_lines(ctx, ui, desc.message, inner, font_px(ui)))
      ui_label(ctx, line.c_str());
    ui_space(ctx, 6.0f);
  }

  i32 count = 0;
  while (count < 4 && desc.buttons[count] != nullptr)
    count++;
  // The number is part of the id, so two buttons with the same text still differ.
  std::string labels[4];
  for (i32 i = 0; i < count; i++)
    labels[i] = std::string(desc.buttons[i]) + "##popup" + std::to_string(i);
  if (count > 0 && count <= 3)
    ui_row(ctx, count);
  i32 pick = -1;
  for (i32 i = 0; i < count; i++) {
    if (ui_button(ctx, labels[i].c_str()))
      pick = i;
  }
  if (first && count > 0) {
    const i32 def = desc.default_button >= 0 && desc.default_button < count ? desc.default_button : 0;
    ui.focus = hash_id(ui.panel_id, labels[def].c_str());
  }

  i32 result = -1;
  bool close = false;
  if (pick >= 0) {
    close = true;
    result = pick;
  } else if (!first && ui_back(ctx)) {
    // Not on the first frame: the press that opened the popup must not close it.
    close = true;
    result = desc.cancel_button >= 0 && desc.cancel_button < count ? desc.cancel_button : -1;
  }
  if (close)
    open = false;
  ui_popup_end(ctx);
  return result;
}

// --- toast ---

void ui_toast(context &ctx, const char *text, const ui_toast_desc &desc) {
  if (text == nullptr || *text == '\0')
    return;
  ui_state &ui = ctx.ui;
  ui.toasts.push_back(ui_toast_rec{.text = text,
                                   .kind = desc.kind,
                                   .age = 0.0f,
                                   .life = desc.seconds > 0.0f ? desc.seconds : ui.style.toast_seconds});
  const usize cap = (usize)std::max(ui.style.toast_max, 1);
  while (ui.toasts.size() > cap)
    ui.toasts.erase(ui.toasts.begin());
}

void ui_toast_clear(context &ctx) { ctx.ui.toasts.clear(); }

void ui_draw_toasts(context &ctx) {
  ui_state &ui = ctx.ui;
  if (ui.toasts.empty())
    return;
  const f32 dt = ctx.time.dt_real; // real time: toasts run while the game is paused
  for (ui_toast_rec &t : ui.toasts)
    t.age += dt;
  std::erase_if(ui.toasts, [](const ui_toast_rec &t) { return t.age >= t.life; });

  const ui_style &st = ui.style;
  const vec2 screen = screen_size(ctx);
  const vec2 margin = st.toast_margin * st.scale;
  const f32 pad = sc(ui, 14.0f);
  const f32 gap = sc(ui, 8.0f);
  const f32 bar = sc(ui, 5.0f);
  const f32 size = font_px(ui);
  const f32 max_box = std::min(sc(ui, st.toast_width), screen.x - 2.0f * margin.x);
  const bool from_bottom = st.toast_anchor.y >= 0.5f;
  const bool centred_x = std::abs(st.toast_anchor.x - 0.5f) < 0.25f;
  const f32 slide_dir = st.toast_anchor.x >= 0.5f ? 1.0f : -1.0f;
  f32 y = from_bottom ? screen.y - margin.y : margin.y;

  std::vector<ui_cmd> cmds;
  // The newest sits at the anchor, older ones stack away from it.
  for (usize n = ui.toasts.size(); n-- > 0;) {
    const ui_toast_rec &t = ui.toasts[n];
    const f32 text_w = max_box - 2.0f * pad - bar - pad * 0.6f;
    const std::vector<std::string> lines = wrap_lines(ctx, ui, t.text.c_str(), text_w, size);
    f32 widest = 0.0f;
    f32 line_h = 0.0f;
    for (const std::string &l : lines) {
      const vec2 m = measure(ctx, ui, l, size);
      widest = std::max(widest, m.x);
      line_h = std::max(line_h, m.y);
    }
    const vec2 box_size{widest + 2.0f * pad + bar + pad * 0.6f,
                        line_h * (f32)lines.size() + 2.0f * pad};

    const f32 appear = ease_apply(ease::out_cubic, clamp(t.age / 0.25f, 0.0f, 1.0f));
    const f32 fade = clamp((t.life - t.age) / 0.35f, 0.0f, 1.0f) * appear;
    vec2 pos{lerp(margin.x, screen.x - margin.x - box_size.x, st.toast_anchor.x),
             from_bottom ? y - box_size.y : y};
    if (centred_x)
      pos.y += (from_bottom ? 1.0f : -1.0f) * (1.0f - appear) * box_size.y * 0.6f;
    else
      pos.x += slide_dir * (1.0f - appear) * box_size.x * 0.5f;
    y = from_bottom ? y - box_size.y - gap : y + box_size.y + gap;

    ui_cmd back{};
    back.kind = ui_cmd::skin;
    back.area = {pos, box_size};
    back.look = st.toast.normal;
    back.look.color.a *= fade;
    back.look.outline.a *= fade;
    back.shader = st.toast.shader;
    cmds.push_back(std::move(back));

    ui_cmd stripe{};
    stripe.kind = ui_cmd::skin;
    stripe.area = {pos + vec2{pad * 0.6f, pad * 0.8f}, {bar, box_size.y - pad * 1.6f}};
    stripe.look.color = st.toast_accent[std::clamp((i32)t.kind, 0, 3)];
    stripe.look.color.a *= fade;
    stripe.look.roundness = 1.0f;
    cmds.push_back(std::move(stripe));

    for (usize i = 0; i < lines.size(); i++) {
      ui_cmd text{};
      text.kind = ui_cmd::text;
      text.str = lines[i];
      text.size = size;
      text.area.pos = pos + vec2{pad * 0.6f + bar + pad * 0.6f, pad + line_h * (f32)i};
      text.color = st.toast.text;
      text.color.a *= fade;
      cmds.push_back(std::move(text));
    }
  }
  flush(ctx, ui, cmds);
}
} // namespace njin

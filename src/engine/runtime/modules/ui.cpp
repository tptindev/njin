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
#include "njin_sdf2d.h"
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

void append_utf8(std::string &out, i32 c) {
  const u32 u = (u32)c;
  if (u < 0x80) {
    out += (char)u;
  } else if (u < 0x800) {
    out += (char)(0xC0 | (u >> 6));
    out += (char)(0x80 | (u & 0x3F));
  } else if (u < 0x10000) {
    out += (char)(0xE0 | (u >> 12));
    out += (char)(0x80 | ((u >> 6) & 0x3F));
    out += (char)(0x80 | (u & 0x3F));
  } else {
    out += (char)(0xF0 | (u >> 18));
    out += (char)(0x80 | ((u >> 12) & 0x3F));
    out += (char)(0x80 | ((u >> 6) & 0x3F));
    out += (char)(0x80 | (u & 0x3F));
  }
}

// Characters (not bytes) in UTF-8 `s`; the byte where its last one starts.
i32 utf8_count(const std::string &s) {
  i32 n = 0;
  for (const char ch : s)
    n += ((u8)ch & 0xC0) != 0x80 ? 1 : 0;
  return n;
}
usize utf8_last(const std::string &s) {
  usize at = s.size();
  while (at > 0 && ((u8)s[--at] & 0xC0) == 0x80) {
  }
  return at;
}

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

void draw_skin(context &ctx, const ui_cmd &c, f32 scale) {
  // What is under a flat skin blurred first (ui_skin::blur), cut to its corners.
  if (c.look.blur > 0.0f && texture_slot_of(ctx.texture, c.look.texture) == nullptr) {
    const f32 shorter = std::min(c.area.size.x, c.area.size.y);
    const f32 round = c.look.radius > 0.0f && shorter > 0.0f
                          ? std::min(1.0f, 2.0f * c.look.radius * scale / shorter)
                          : c.look.roundness;
    draw_backdrop_blur(ctx, c.area, c.look.blur * scale, round * shorter * 0.5f);
  }
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
    } else if (ctx.view.canvas_on) {
      for (f32 &a : area)
        a *= ctx.view.canvas_scale;
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
    if (s.border > 0.0f && s.border_fit && src.height > 0.0f) {
      // The corners scaled with the widget's height against the picture's.
      const f32 k = dest.height / src.height;
      const f32 bs = std::min(s.border, 0.5f * std::min(src.width, src.height));
      const f32 bd = std::min({bs * k, 0.5f * dest.width, 0.5f * dest.height});
      const f32 sx[4] = {src.x, src.x + bs, src.x + src.width - bs, src.x + src.width};
      const f32 sy[4] = {src.y, src.y + bs, src.y + src.height - bs, src.y + src.height};
      const f32 dx[4] = {dest.x, dest.x + bd, dest.x + dest.width - bd, dest.x + dest.width};
      const f32 dy[4] = {dest.y, dest.y + bd, dest.y + dest.height - bd, dest.y + dest.height};
      for (i32 j = 0; j < 3; ++j)
        for (i32 i = 0; i < 3; ++i) {
          const Rectangle from{sx[i], sy[j], sx[i + 1] - sx[i], sy[j + 1] - sy[j]};
          const Rectangle to{dx[i], dy[j], dx[i + 1] - dx[i], dy[j + 1] - dy[j]};
          if (from.width > 0.0f && from.height > 0.0f && to.width > 0.0f && to.height > 0.0f)
            DrawTexturePro(tex->texture, from, to, Vector2{0.0f, 0.0f}, 0.0f, color);
        }
    } else if (s.border > 0.0f) {
      const i32 b = (i32)s.border;
      const NPatchInfo patch{src, b, b, b, b, NPATCH_NINE_PATCH};
      DrawTextureNPatch(tex->texture, patch, dest, Vector2{0.0f, 0.0f}, 0.0f, color);
    } else {
      DrawTexturePro(tex->texture, src, dest, Vector2{0.0f, 0.0f}, 0.0f, color);
    }
  } else {
    // A radius in pixels is raylib's roundness of the shorter side.
    const f32 shorter = std::min(dest.width, dest.height);
    const f32 round = s.radius > 0.0f && shorter > 0.0f ? std::min(1.0f, 2.0f * s.radius * scale / shorter)
                                                        : s.roundness;
    // Smooth shapes (draw_set_smooth_shapes()) when no skin shader is on: the
    // same box and the same outline, round the outside of `dest` as raylib's.
    const f32 px = round * shorter * 0.5f;
    const bool smooth = sh == nullptr;
    if (s.color.a > 0.0f && !(smooth && sdf2d_rounded_rect(ctx, c.area, px, 0.0f, s.color))) {
      if (round > 0.0f)
        DrawRectangleRounded(dest, round, 8, color);
      else
        DrawRectangleRec(dest, color);
    }
    if (s.outline.a > 0.0f && s.outline_width > 0.0f) {
      const f32 w = s.outline_width;
      const rect out{c.area.pos - vec2{w, w}, c.area.size + vec2{2.0f * w, 2.0f * w}};
      if (!(smooth && sdf2d_rounded_rect(ctx, out, round > 0.0f ? px + w : 0.0f, w, s.outline))) {
        Color line{};
        to_raylib(s.outline, line);
        DrawRectangleRoundedLinesEx(dest, round, 8, w, line);
      }
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

// A panel's commands at `a` of their opacity (a panel entering).
void fade(std::vector<ui_cmd> &cmds, f32 a) {
  for (ui_cmd &c : cmds) {
    c.look.color.a *= a;
    c.look.outline.a *= a;
    c.look.blur *= a;
    c.color.a *= a;
    c.track.a *= a;
  }
}

void flush(context &ctx, const ui_state &ui, const std::vector<ui_cmd> &cmds) {
  for (const ui_cmd &c : cmds) {
    switch (c.kind) {
    case ui_cmd::skin:
      draw_skin(ctx, c, ui.style.scale * ui.fit);
      break;
    case ui_cmd::ring:
      draw_ring(c);
      break;
    case ui_cmd::clip_begin:
      clip_begin(ctx, c.area);
      break;
    case ui_cmd::clip_end:
      clip_end(ctx);
      break;
    case ui_cmd::text:
      draw_text(ctx, c.str.c_str(), c.area.pos, c.size, c.color, c.font_set ? c.font : ui.style.font);
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
  ui.last_panel_ids = std::move(ui.panel_ids);
  ui.panel_ids.clear();
  ui.last_shown = std::move(ui.shown);
  ui.shown.clear();
  ui.last_scrolls = std::move(ui.scrolls);
  ui.scrolls.clear();
  ui.last_scroll_panel = std::move(ui.scroll_panel);
  ui.scroll_panel.clear();
  ui.last_through = std::move(ui.through);
  ui.through.clear();
  ui.last_through_panel = std::move(ui.through_panel);
  ui.through_panel.clear();
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
  // In the canvas's units when there is one: the panels were built in them.
  const vec2 mouse = input_mouse_pos(ctx.input) * (1.0f / ctx.view.canvas_scale);
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
  // A text field being typed in: the keys are its, not navigation and not
  // the game's (it is not drawn any more: it lets them go).
  if (ui.editing != 0 && !ui.edit_drawn)
    ui.editing = 0;
  ui.edit_drawn = false;
  ui.edit_typed.clear();
  ui.edit_back = 0;
  ui.edit_enter = ui.edit_escape = false;
  if (ui.editing != 0) {
    for (i32 i = 0; i < text_count(ctx); i++)
      if (const i32 c = text_char(ctx, i); c >= 32)
        append_utf8(ui.edit_typed, c);
    ui.edit_back = input_backspaces(ctx.input);
    ui.edit_enter = key_pressed(ctx, key_enter) || key_pressed(ctx, key_kp_enter);
    ui.edit_escape = key_pressed(ctx, key_escape);
    for (i32 k = key_none + 1; k < key_count; k++)
      key_consume(ctx, (key_code)k);
    ui.up = ui.down = ui.left = ui.right = false;
    ui.accept = ui.accept_held = ui.back = false;
  }

  // The panels overlap in the order they were drawn: under the mouse, the one
  // drawn last is on top and takes it; the ones under it do not.
  ui.mouse_top = 0;
  for (size_t i = ui.last_panels.size(); i-- > 0;)
    if (inside(mouse, ui.last_panels[i])) {
      ui.mouse_top = ui.last_panel_ids[i];
      break;
    }
  ui.mouse_over = ui.mouse_top != 0;
  // Something in a panel takes the mouse only where that panel is on top; one
  // marked outside any panel (0) takes it wherever it is.
  const auto reaches = [&](u64 panel) { return panel == 0 || panel == ui.mouse_top; };
  // The wheel over a scroll area scrolls it, and the game does not see it.
  ui.wheel = 0.0f;
  for (size_t i = 0; i < ui.last_scrolls.size(); i++)
    if (inside(mouse, ui.last_scrolls[i]) && reaches(ui.last_scroll_panel[i])) {
      ui.wheel = mouse_wheel(ctx);
      mouse_wheel_consume(ctx);
      break;
    }
  // Over a panel the left button is the UI's, but where a panel lets it through
  // (ui_let_through: a map drawn in it, dragged by the game).
  bool through = false;
  for (size_t i = 0; i < ui.last_through.size(); i++)
    through = through || (inside(mouse, ui.last_through[i]) && reaches(ui.last_through_panel[i]));
  // A scroll bar's thumb held keeps it, the mouse off its panel or not (let
  // go here too, in case its area is not drawn any more).
  if (ui.scroll_drag != 0 && !ui.mouse_held && !ui.mouse_released)
    ui.scroll_drag = 0;
  if ((ui.mouse_over && !through) || ui.scroll_drag != 0)
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

// The width of the next slot: a column of the current row, or the line.
f32 slot_width(const ui_state &ui) {
  const f32 pad = sc(ui, ui.style.padding);
  const f32 gap = sc(ui, ui.style.spacing);
  const f32 inner = ui.panel.size.x - 2.0f * pad - (ui.in_scroll ? ui.scroll_reserve : 0.0f);
  if (ui.row_cols > 0)
    return (inner - gap * (f32)(ui.row_cols - 1)) / (f32)ui.row_cols;
  return inner;
}

// Where a row left unfinished ends: as tall as a widget, or its tallest.
f32 row_end(const ui_state &ui) {
  return ui.row_y + std::max(sc(ui, ui.style.widget_height), ui.row_h) + sc(ui, ui.style.spacing);
}

// Takes the next slot of height `h` in the panel (a column of the current
// row, or a full-width line). A row is as tall as its tallest widget.
rect place(ui_state &ui, f32 h) {
  const f32 pad = sc(ui, ui.style.padding);
  const f32 gap = sc(ui, ui.style.spacing);
  const f32 inner = slot_width(ui);
  if (ui.row_cols > 0) {
    const f32 w = inner;
    const rect r{{ui.panel.pos.x + pad + (w + gap) * (f32)ui.row_index, ui.row_y}, {w, h}};
    ui.row_h = std::max(ui.row_h, h);
    if (++ui.row_index >= ui.row_cols) {
      ui.row_cols = 0;
      ui.cursor = ui.row_y + ui.row_h + gap;
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

// A scroll area's bar (ui_style::scrollbar_width): the rail down its right
// edge, and the thumb on it, as tall as the part that shows, where `off` is.
rect scroll_rail(const ui_state &ui, rect area) {
  const f32 w = sc(ui, ui.style.scrollbar_width);
  const f32 m = std::min(sc(ui, 2.0f), area.size.y * 0.25f);
  return {{area.pos.x + area.size.x - w - m, area.pos.y + m}, {w, area.size.y - 2.0f * m}};
}

rect scroll_thumb(const ui_state &ui, rect area, f32 off, f32 content) {
  const rect rail = scroll_rail(ui, area);
  const f32 h = std::min(rail.size.y, std::max(2.0f * rail.size.x, rail.size.y * area.size.y / content));
  const f32 most = std::max(1.0f, content - area.size.y);
  return {{rail.pos.x, rail.pos.y + (rail.size.y - h) * clamp(off / most, 0.0f, 1.0f)}, {rail.size.x, h}};
}

void push_text(context &ctx, ui_state &ui, const std::string &text, vec2 pos, rgba color,
               f32 size = 0.0f) {
  ui_cmd c{};
  c.kind = ui_cmd::text;
  c.str = text;
  c.font = ui.style.font;
  c.font_set = true;
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

// The size `text` is drawn at to fit `room` pixels: `size`, or smaller down
// to `least` of it.
f32 fit_size(const context &ctx, const ui_state &ui, const std::string &text, f32 size, f32 room,
             f32 least = 0.6f) {
  const f32 w = measure(ctx, ui, text, size).x;
  if (w <= room || w <= 0.0f || room <= 0.0f)
    return size;
  return size * std::max(least, room / w);
}

// Text centred in `area`, made smaller when it is wider than the area.
void text_center_fit(context &ctx, ui_state &ui, const std::string &text, rect area, rgba color) {
  const f32 size = fit_size(ctx, ui, text, font_px(ui), area.size.x - 2.0f * sc(ui, 6.0f));
  const vec2 m = measure(ctx, ui, text, size);
  push_text(ctx, ui, text, rect_center(area) - m * 0.5f, color, size);
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

// Places a focusable widget (`h` pixels high, 0 a widget's height) and works
// out its state from the input.
interaction interact(context &ctx, const char *label, bool enabled, bool adjustable, f32 h = 0.0f) {
  ui_state &ui = ctx.ui;
  interaction it;
  it.id = hash_id(ui.panel_id, label);
  it.area = place(ui, h > 0.0f ? h : sc(ui, ui.style.widget_height));
  if (!enabled) {
    it.state = state_disabled;
    return it;
  }
  if (ui.panel_inert) // leaving: shown, not used
    return it;
  // In a scroll area, only where it shows.
  const bool shown_here = (!ui.in_scroll || inside(ui.mouse, ui.scroll_area)) && !ui.panel_covered;
  if (!ui.panel_navigable) {
    // HUD panel: mouse only, never focused, not reachable by the keys.
    it.hover = ui.modal_last == 0 && inside(ui.mouse, it.area) && shown_here;
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
  it.hover = inside(ui.mouse, it.area) && shown_here;
  // Focused by the keys out of a scroll area's view: scrolled to it next frame.
  if (ui.in_scroll && ui.focus == it.id) {
    f32 &off = ui.scroll_offset[ui.scroll_id];
    const rect &a = ui.scroll_area;
    if (it.area.pos.y < a.pos.y)
      off -= a.pos.y - it.area.pos.y;
    else if (it.area.pos.y + it.area.size.y > a.pos.y + a.size.y)
      off += it.area.pos.y + it.area.size.y - (a.pos.y + a.size.y);
  }
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
  const ui_skin no_face{.color = {0.0f, 0.0f, 0.0f, 0.0f}, .roundness = 0.3f};
  s.tab.normal = s.tab.disabled = no_face;
  s.tab.focused = {.color = {1.0f, 1.0f, 1.0f, 0.06f}, .roundness = 0.3f, .outline = accent, .outline_width = 2.0f};
  s.tab.pressed = {.color = {1.0f, 1.0f, 1.0f, 0.10f}, .roundness = 0.3f};
  s.tab.text = {0.62f, 0.66f, 0.75f, 1.0f};
  s.tab.text_focused = {0.90f, 0.92f, 0.97f, 1.0f};
  s.tab.text_disabled = s.button.text_disabled;
  s.tab_open = {.color = {0.16f, 0.18f, 0.24f, 1.0f}, .roundness = 0.3f};
  s.tab_open_text = {1.0f, 1.0f, 1.0f, 1.0f};
  s.tab_bar = {.color = {0.0f, 0.0f, 0.0f, 0.28f}, .roundness = 0.35f};
  s.tab_line = {.color = accent, .roundness = 1.0f};
  return s;
}

void ui_style_set(context &ctx, const ui_style &style) { ctx.ui.style = style; }

void ui_canvas_set(context &ctx, const ui_canvas_desc &desc) {
  view_state &v = ctx.view;
  v.canvas_height = std::max(0.0f, desc.height);
  v.canvas_min_width = std::max(0.0f, desc.min_width);
  v.canvas_min = std::max(0.05f, desc.min_scale);
  v.canvas_max = std::max(v.canvas_min, desc.max_scale);
}

void ui_canvas_begin(const context &ctx) {
  ctx.view.canvas_depth++;
  view_canvas_sync(ctx.view);
}

void ui_canvas_end(const context &ctx) {
  if (ctx.view.canvas_depth <= 0)
    return;
  ctx.view.canvas_depth--;
  view_canvas_sync(ctx.view);
}

f32 ui_canvas_scale(const context &ctx) { return ctx.view.canvas_scale; }
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
  ui.panel_covered = ui.mouse_top != 0 && ui.mouse_top != ui.panel_id;
  ui.panel_title = desc.title;
  ui.panel_background = desc.background;
  ui.panel_navigable = desc.navigable;
  ui.cmds.clear();
  ui.row_cols = 0;
  const vec2 screen = screen_size(ctx);
  // A panel taller than the room it has where it stands (its anchor, pivot
  // and offset: one hung from above the bottom edge has only the screen
  // above that point) would run off it: shrink its sizes (text included) by
  // the factor that makes it fit, down to half. The height it would have
  // unshrunk is known from the frame before.
  ui.fit = 1.0f;
  const f32 margin = sc(ui, panel_screen_margin);
  const auto nat = ui.natural.find(ui.panel_id);
  if (nat != ui.natural.end() && nat->second > 0.0f) {
    const f32 at = screen.y * desc.anchor.y + desc.offset.y; // the pivot's height on the screen
    f32 room = screen.y - 2.0f * margin;
    if (desc.pivot.y > 0.0f)
      room = std::min(room, (at - margin) / desc.pivot.y);
    if (desc.pivot.y < 1.0f)
      room = std::min(room, (screen.y - margin - at) / (1.0f - desc.pivot.y));
    ui.fit = std::clamp(std::max(room, 0.0f) / nat->second, min_panel_fit, 1.0f);
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
  // Shrunk as far as it goes and still too tall where it stands: moved down
  // to keep its top (its title, its close box) on the screen.
  if (ui.fit <= min_panel_fit && ui.panel.pos.y < margin)
    ui.panel.pos.y = margin;
  // Entering: slid from `enter_from` and faded in, over `enter_seconds` from
  // the first frame it is drawn after a frame without it.
  ui.panel_alpha = 1.0f;
  ui.shown.push_back(ui.panel_id);
  if (desc.enter_seconds > 0.0f) {
    const bool was = std::find(ui.last_shown.begin(), ui.last_shown.end(), ui.panel_id) != ui.last_shown.end();
    const auto age = ui.enter_age.find(ui.panel_id);
    f32 t = 1.0f;
    if (!was) {
      ui.enter_age[ui.panel_id] = 0.0f;
      t = 0.0f;
    } else if (age != ui.enter_age.end()) {
      age->second += ctx.time.dt_real;
      t = std::min(1.0f, age->second / desc.enter_seconds);
      if (t >= 1.0f)
        ui.enter_age.erase(age);
    }
    const f32 e = ease_apply(ease::out_cubic, t);
    ui.panel.pos += desc.enter_from * (1.0f - e);
    ui.panel_alpha = e;
  }
  // Leaving: the other way, and dead to the mouse.
  ui.panel_inert = desc.leaving > 0.0f || desc.inert;
  if (desc.leaving > 0.0f) {
    const f32 e = ease_apply(ease::in_cubic, std::clamp(desc.leaving, 0.0f, 1.0f));
    ui.panel.pos += desc.enter_from * e;
    ui.panel_alpha *= 1.0f - e;
  }
  ui.cursor = ui.panel.pos.y + sc(ui, ui.style.padding);
  if (desc.title != nullptr) {
    const f32 title_size = font_px(ui) * 1.3f;
    const vec2 m = measure(ctx, ui, desc.title, title_size);
    push_text(ctx, ui, desc.title, {ui.panel.pos.x + (width - m.x) * 0.5f, ui.cursor},
              ui.style.panel.text, title_size);
    ui.cursor += m.y + sc(ui, ui.style.spacing) * 1.6f;
  }
  if (desc.open != nullptr) {
    // The close box, top right: mouse only, like a HUD button.
    const f32 s = font_px(ui) * 1.3f;
    const f32 pad = sc(ui, ui.style.padding) * 0.5f;
    const rect box{{ui.panel.pos.x + width - pad - s, ui.panel.pos.y + pad}, {s, s}};
    const u64 id = hash_id(ui.panel_id, "##close");
    const bool hover = ui.modal_last == 0 && !ui.panel_inert && !ui.panel_covered && inside(ui.mouse, box);
    if (hover && ui.mouse_pressed)
      ui.pressed = id;
    if (ui.pressed == id && ui.mouse_released && hover) {
      *desc.open = false;
      play(ctx, ui.style.sound_back);
    }
    const widget_state st = ui.pressed == id && ui.mouse_held ? state_pressed : hover ? state_focused : state_normal;
    push_skin(ui, ui.style.button, st, box);
    text_center(ctx, ui, "X", box, text_for(ui.style.button, st));
    ui.cursor = std::max(ui.cursor, box.pos.y + s + sc(ui, ui.style.spacing));
  }
}

void ui_end(context &ctx) {
  ui_state &ui = ctx.ui;
  if (!ui.in_panel) {
    NJIN_WARN("ui_end without ui_begin");
    return;
  }
  if (ui.in_scroll) {
    NJIN_WARN("ui_end: a scroll area left open; close it with ui_scroll_end");
    ui_scroll_end(ctx);
  }
  ui.in_panel = false;
  if (ui.row_cols > 0) { // a row left unfinished
    ui.cursor = row_end(ui);
    ui.row_cols = 0;
  }
  const f32 height = ui.cursor - sc(ui, ui.style.spacing) + sc(ui, ui.style.padding) - ui.panel.pos.y;
  ui.heights[ui.panel_id] = height;
  ui.natural[ui.panel_id] = height / ui.fit;
  ui.fit = 1.0f;
  rect bg = ui.panel;
  bg.size.y = height;
  ui.panel_done = bg;
  if (!ui.panel_inert) {
    ui.panels.push_back(bg);
    ui.panel_ids.push_back(ui.panel_id);
    if (ui.panel_navigable)
      ui.navigable_now = true;
  }
  if (ui.panel_alpha < 1.0f)
    fade(ui.cmds, ui.panel_alpha);
  if (ui.panel_background) {
    ui_cmd c{};
    c.kind = ui_cmd::skin;
    c.area = bg;
    c.look = ui.style.panel.normal;
    c.shader = ui.style.panel.shader;
    std::vector<ui_cmd> back{c};
    if (ui.panel_alpha < 1.0f)
      fade(back, ui.panel_alpha);
    flush(ctx, ui, back);
  }
  flush(ctx, ui, ui.cmds);
  ui.cmds.clear();
  ui.panel_alpha = 1.0f;
  ui.panel_inert = false;
}

void ui_row(context &ctx, i32 columns) {
  ui_state &ui = ctx.ui;
  require_panel(ui, "ui_row");
  if (columns <= 0)
    return;
  ui.row_cols = columns;
  ui.row_index = 0;
  ui.row_y = ui.cursor;
  ui.row_h = 0.0f;
}

namespace {
// A line of text (ui_label, ui_text) in the next slot, `size` pixels, placed
// across it by `align` (0 left, 1 right). In a row it stands as tall as the
// widgets beside it, its text in the middle: level with a button's. Wider
// than its slot, it is wrapped there, line under line, the slot (and its
// row) as tall as the lines.
void text_slot(context &ctx, ui_state &ui, const char *text, f32 size, rgba color, f32 align) {
  const bool in_row = ui.row_cols > 0;
  const f32 room = slot_width(ui);
  const vec2 m = measure(ctx, ui, text, size);
  std::vector<std::string> lines;
  if (m.x > room && room > 0.0f)
    lines = text_wrap(ctx, text, size, room, ui.style.font);
  if (lines.size() <= 1) {
    const rect r = place(ui, in_row ? sc(ui, ui.style.widget_height) : m.y);
    const f32 y = in_row ? rect_center(r).y - m.y * 0.5f : r.pos.y;
    push_text(ctx, ui, text, {r.pos.x + (r.size.x - m.x) * align, y}, color, size);
    return;
  }
  const f32 line_h = measure(ctx, ui, lines[0], size).y * 1.1f;
  const f32 tall = line_h * (f32)lines.size();
  const rect r = place(ui, in_row ? std::max(sc(ui, ui.style.widget_height), tall) : tall);
  f32 y = r.pos.y + (r.size.y - tall) * 0.5f;
  for (const std::string &line : lines) {
    const f32 w = measure(ctx, ui, line, size).x;
    push_text(ctx, ui, line, {r.pos.x + std::max(0.0f, r.size.x - w) * align, y}, color, size);
    y += line_h;
  }
}
} // namespace

void ui_label(context &ctx, const char *text) {
  ui_state &ui = ctx.ui;
  require_panel(ui, "ui_label");
  if (text == nullptr)
    return;
  text_slot(ctx, ui, text, font_px(ui), ui.style.label.text, 0.0f);
}

void ui_space(context &ctx, f32 height) {
  ui_state &ui = ctx.ui;
  require_panel(ui, "ui_space");
  const f32 pad = sc(ui, ui.style.padding);
  ui.last_rect = rect{{ui.panel.pos.x + pad, ui.cursor},
                      {ui.panel.size.x - 2.0f * pad - (ui.in_scroll ? ui.scroll_reserve : 0.0f), sc(ui, height)}};
  ui.cursor += sc(ui, height);
}

bool ui_button(context &ctx, const char *label, bool enabled, bool selected) {
  ui_state &ui = ctx.ui;
  require_panel(ui, "ui_button");
  if (label == nullptr)
    return false;
  const interaction it = interact(ctx, label, enabled, false);
  // Selected (a tab, a tool that is on): its pressed face while nothing else shows.
  const widget_state face = selected && it.state == state_normal ? state_pressed : it.state;
  // Held down, drawn a little smaller (ui_style::press_scale).
  rect area = it.area;
  if (it.state == state_pressed && ui.style.press_scale != 1.0f) {
    const vec2 c = rect_center(area);
    area.size = area.size * ui.style.press_scale;
    area.pos = c - area.size * 0.5f;
  }
  // Into its hover face and out of it over ui_style::hover_seconds.
  f32 hover = -1.0f; // not blending
  if (ui.style.hover_seconds > 0.0f && (face == state_normal || face == state_focused)) {
    const f32 to = face == state_focused ? 1.0f : 0.0f;
    const auto at = ui.hover_amount.find(it.id);
    f32 h = at != ui.hover_amount.end() ? at->second : 0.0f;
    const f32 step = ctx.time.dt_real / ui.style.hover_seconds;
    h = to > h ? std::min(to, h + step) : std::max(to, h - step);
    if (h <= 0.0f) {
      if (at != ui.hover_amount.end())
        ui.hover_amount.erase(at);
    } else {
      ui.hover_amount[it.id] = h;
    }
    if (h > 0.0f && h < 1.0f)
      hover = ease_apply(ease::in_out_sine, h);
  }
  if (hover >= 0.0f) {
    const ui_skin &a = ui.style.button.normal, &b = ui.style.button.focused;
    ui_cmd c{};
    c.kind = ui_cmd::skin;
    c.area = area;
    c.look = hover < 0.5f ? a : b;
    c.look.color = lerp(a.color, b.color, hover);
    c.look.outline = lerp(a.outline, b.outline, hover);
    c.look.outline_width = lerp(a.outline_width, b.outline_width, hover);
    c.shader = ui.style.button.shader;
    c.state = hover < 0.5f ? (f32)state_normal : (f32)state_focused;
    ui.cmds.push_back(std::move(c));
    text_center_fit(ctx, ui, shown(label), area, lerp(ui.style.button.text, ui.style.button.text_focused, hover));
  } else {
    push_skin(ui, ui.style.button, face, area);
    text_center_fit(ctx, ui, shown(label), area, text_for(ui.style.button, face));
  }
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
  // Its body: its own off and on faces if the style has them, else the track and the fill.
  if (ui.style.toggle_own)
    push_skin(ui, value ? ui.style.toggle_on : ui.style.toggle_off, it.state, b, value ? 1.0f : 0.0f);
  else
    push_skin(ui, ui.style.track, it.state, b, value ? 1.0f : 0.0f);
  // A switch: the knob sits left when off, right when on.
  const f32 k = box - sc(ui, 6.0f);
  const rect knob{{value ? b.pos.x + b.size.x - k - sc(ui, 3.0f) : b.pos.x + sc(ui, 3.0f),
                   b.pos.y + sc(ui, 3.0f)},
                  {k, k}};
  if (value && !ui.style.toggle_own)
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

namespace {
bool visible(const ui_skin &s) {
  return s.texture.id != 0 || s.color.a > 0.0f || (s.outline.a > 0.0f && s.outline_width > 0.0f);
}

void push_face(ui_state &ui, const ui_skin &skin, shader_handle shader, widget_state s, rect area) {
  ui_cmd c{};
  c.kind = ui_cmd::skin;
  c.area = area;
  c.look = skin;
  c.shader = shader;
  c.state = (f32)s;
  ui.cmds.push_back(std::move(c));
}

template <typename F>
bool ui_tabs_impl(context &ctx, const char *id, i32 &index, i32 count, F &&label_of, const ui_tabs_desc &desc) {
  ui_state &ui = ctx.ui;
  require_panel(ui, "ui_tabs");
  ui.tab_icons.clear();
  if (id == nullptr || count <= 0)
    return false;
  const ui_style &st = ui.style;
  const interaction it =
      interact(ctx, id, true, true, sc(ui, desc.height > 0.0f ? desc.height : st.widget_height));
  // The tabs, side by side in the bar (inside its ground, when it has one).
  const f32 inset = visible(st.tab_bar) ? sc(ui, 3.0f) : 0.0f;
  const rect inner{it.area.pos + vec2{inset, inset}, it.area.size - vec2{2.0f * inset, 2.0f * inset}};
  const f32 gap = sc(ui, st.tab_spacing);
  const f32 w = (inner.size.x - gap * (f32)(count - 1)) / (f32)count;
  const auto tab_at = [&](f32 k) { return rect{{inner.pos.x + (w + gap) * k, inner.pos.y}, {w, inner.size.y}}; };
  i32 under = -1; // the tab under the mouse
  if (it.hover && w > 0.0f) {
    const f32 x = ui.mouse.x - inner.pos.x;
    const i32 k = (i32)std::floor(x / (w + gap));
    if (k >= 0 && k < count && x - (f32)k * (w + gap) <= w)
      under = k;
  }
  // Picked: the tab clicked; with the keys, the one beside, or the next.
  const i32 was = std::clamp(index, 0, count - 1);
  i32 open = was;
  if (ui.focus == it.id && ui.adjust != 0)
    open = std::clamp(open + ui.adjust, 0, count - 1);
  else if (it.clicked) {
    const bool mouse_click = ui.pressed == it.id && ui.mouse_released;
    if (!mouse_click)
      open = (open + 1) % count;
    else if (under >= 0)
      open = under;
  }
  const bool changed = open != was;
  if (changed) {
    index = open;
    play(ctx, st.sound_accept);
  }
  // The open face slides to the open tab.
  f32 &at = ui.tab_slide.try_emplace(it.id, (f32)open).first->second;
  if (st.tab_slide_seconds > 0.0f) {
    at += ((f32)open - at) * (1.0f - std::exp(-ctx.time.dt_real * 5.0f / st.tab_slide_seconds));
    if (std::abs((f32)open - at) < 0.002f)
      at = (f32)open;
  } else {
    at = (f32)open;
  }
  if (visible(st.tab_bar))
    push_face(ui, st.tab_bar, st.tab.shader, state_normal, it.area);
  const bool held = ui.pressed == it.id && ui.mouse_held;
  for (i32 k = 0; k < count; ++k) {
    const widget_state s = k == under ? (held ? state_pressed : state_focused) : state_normal;
    if (k != open && visible(skin_for(st.tab, s)))
      push_skin(ui, st.tab, s, tab_at((f32)k));
  }
  const rect face = tab_at(at);
  ui_skin open_skin = st.tab_open;
  if (ui.focus == it.id && !it.hover && st.tab.focused.outline.a > 0.0f) { // chosen with the keys
    open_skin.outline = st.tab.focused.outline;
    open_skin.outline_width = st.tab.focused.outline_width;
  }
  if (visible(open_skin))
    push_face(ui, open_skin, st.tab.shader, state_pressed, face);
  if (visible(st.tab_line)) {
    const f32 lh = sc(ui, st.tab_line_height);
    push_face(ui, st.tab_line, st.tab.shader, state_pressed,
              {{face.pos.x + face.size.x * 0.15f, face.pos.y + face.size.y - lh}, {face.size.x * 0.7f, lh}});
  }
  // The labels (and the icons' room), each centred in its tab, made smaller
  // when they would not fit.
  const f32 base = desc.font_size > 0.0f ? sc(ui, desc.font_size) : font_px(ui);
  const f32 icon = desc.icon_size > 0.0f ? sc(ui, desc.icon_size) : 0.0f;
  const f32 room = w - 2.0f * sc(ui, 6.0f);
  for (i32 k = 0; k < count; ++k) {
    const rect r = tab_at((f32)k);
    const std::string label = shown(label_of(k));
    const f32 between = icon > 0.0f && !label.empty() ? sc(ui, 6.0f) : 0.0f;
    const f32 size = fit_size(ctx, ui, label, base, room - icon - between, 0.55f);
    const vec2 m = label.empty() ? vec2{} : measure(ctx, ui, label, size);
    const f32 x = r.pos.x + (r.size.x - (icon + between + m.x)) * 0.5f;
    const f32 mid = rect_center(r).y;
    if (icon > 0.0f)
      ui.tab_icons.push_back({{x, mid - icon * 0.5f}, {icon, icon}});
    if (!label.empty()) {
      const rgba c = k == open ? st.tab_open_text : k == under ? st.tab.text_focused : st.tab.text;
      push_text(ctx, ui, label, {x + icon + between, mid - m.y * 0.5f}, c, size);
    }
  }
  return changed;
}
} // namespace

bool ui_tabs(context &ctx, const char *id, i32 &index, std::initializer_list<const char *> labels,
             const ui_tabs_desc &desc) {
  return ui_tabs_impl(ctx, id, index, (i32)labels.size(), [&](i32 i) { return *(labels.begin() + i); }, desc);
}

bool ui_tabs(context &ctx, const char *id, i32 &index, std::span<const std::string> labels,
             const ui_tabs_desc &desc) {
  return ui_tabs_impl(ctx, id, index, (i32)labels.size(), [&](i32 i) { return labels[(size_t)i].c_str(); }, desc);
}

rect ui_tabs_icon_rect(const context &ctx, i32 tab) {
  const std::vector<rect> &r = ctx.ui.tab_icons;
  return tab >= 0 && tab < (i32)r.size() ? r[(size_t)tab] : rect{};
}

void ui_progress(context &ctx, f32 value, const char *text, rgba fill) {
  ui_state &ui = ctx.ui;
  require_panel(ui, "ui_progress");
  const f32 t = clamp(value, 0.0f, 1.0f);
  const rect r = place(ui, sc(ui, ui.style.widget_height) * 0.6f);
  push_skin(ui, ui.style.track, state_normal, r, t);
  if (t > 0.0f) {
    push_skin(ui, ui.style.fill, state_normal, rect{r.pos, {r.size.x * t, r.size.y}}, t);
    if (fill.a > 0.0f)
      ui.cmds.back().look.color = fill;
  }
  if (text != nullptr)
    text_center(ctx, ui, text, r, ui.style.track.text);
}

void ui_text(context &ctx, const char *text, const ui_text_desc &desc) {
  ui_state &ui = ctx.ui;
  require_panel(ui, "ui_text");
  if (text == nullptr)
    return;
  const f32 size = desc.size > 0.0f ? sc(ui, desc.size) : font_px(ui);
  const rgba color = desc.color.a > 0.0f ? desc.color : ui.style.label.text;
  text_slot(ctx, ui, text, size, color, clamp(desc.align, 0.0f, 1.0f));
}

void ui_tooltip(context &ctx, const char *text) {
  if (text != nullptr)
    ctx.ui.tooltip = text;
}

void ui_scroll_begin(context &ctx, const char *id, f32 height) {
  ui_state &ui = ctx.ui;
  require_panel(ui, "ui_scroll_begin");
  if (ui.in_scroll) {
    NJIN_WARN("ui_scroll_begin: '%s' opened inside another scroll area", id != nullptr ? id : "");
    ui_scroll_end(ctx);
  }
  if (ui.row_cols > 0) { // a row left unfinished
    ui.cursor = row_end(ui);
    ui.row_cols = 0;
  }
  const f32 pad = sc(ui, ui.style.padding);
  const rect area{{ui.panel.pos.x + pad, ui.cursor}, {ui.panel.size.x - 2.0f * pad, sc(ui, height)}};
  ui.in_scroll = true;
  ui.scroll_id = hash_id(ui.panel_id, id != nullptr ? id : "scroll");
  ui.scroll_area = area;
  // The wheel, a widget's height a notch; never past what it held last frame.
  f32 &off = ui.scroll_offset[ui.scroll_id];
  if (ui.modal_last == 0 && !ui.panel_covered && inside(ui.mouse, area))
    off -= ui.wheel * sc(ui, ui.style.widget_height);
  const f32 content = ui.scroll_content[ui.scroll_id];
  if (ui.scroll_to_end.count(ui.scroll_id) != 0)
    off = content;
  // The bar: its thumb dragged, or the rail pressed (the thumb jumps there
  // and is held by its middle).
  const f32 bar_w = sc(ui, ui.style.scrollbar_width);
  ui.scroll_reserve = bar_w > 0.0f ? bar_w + sc(ui, ui.style.spacing) * 0.5f : 0.0f;
  ui.scroll_hover = false;
  if (bar_w > 0.0f && content > area.size.y) {
    const rect rail = scroll_rail(ui, area);
    const rect thumb = scroll_thumb(ui, area, off, content);
    const f32 travel = rail.size.y - thumb.size.y;
    const f32 most = content - area.size.y;
    const bool free = ui.modal_last == 0 && !ui.panel_covered && !ui.panel_inert;
    ui.scroll_hover = free && (ui.scroll_drag == ui.scroll_id || inside(ui.mouse, thumb));
    if (free && ui.mouse_pressed && inside(ui.mouse, rail)) {
      ui.scroll_drag = ui.scroll_id;
      ui.scroll_grab = inside(ui.mouse, thumb) ? ui.mouse.y - thumb.pos.y : thumb.size.y * 0.5f;
      ui.pressed = ui.scroll_id;
    }
    if (ui.scroll_drag == ui.scroll_id) {
      if (ui.mouse_held || ui.mouse_pressed) {
        if (travel > 0.0f)
          off = (ui.mouse.y - ui.scroll_grab - rail.pos.y) / travel * most;
      } else {
        ui.scroll_drag = 0;
      }
    }
  }
  off = clamp(off, 0.0f, std::max(0.0f, content - area.size.y));
  push_skin(ui, ui.style.track, state_normal, area);
  ui.cmds.back().look.roundness = 0.0f;
  ui.cmds.back().look.radius = 0.0f;
  ui_cmd clip{};
  clip.kind = ui_cmd::clip_begin;
  clip.area = area;
  ui.cmds.push_back(std::move(clip));
  ui.scroll_from = area.pos.y + sc(ui, ui.style.spacing) * 0.5f;
  ui.cursor = ui.scroll_from - off;
}

void ui_scroll_begin_fit(context &ctx, const char *id, f32 max_height) {
  ui_state &ui = ctx.ui;
  require_panel(ui, "ui_scroll_begin_fit");
  // What it held last frame, from its top's half gap to its last widget's
  // gap, back in pixels before the scale.
  f32 height = max_height;
  const auto it = ui.scroll_content.find(hash_id(ui.panel_id, id != nullptr ? id : "scroll"));
  if (it != ui.scroll_content.end() && sc(ui, 1.0f) > 0.0f)
    height = std::min(max_height, (it->second + sc(ui, ui.style.spacing) * 0.5f) / sc(ui, 1.0f));
  ui_scroll_begin(ctx, id, std::max(0.0f, height));
}

void ui_scroll_end(context &ctx) {
  ui_state &ui = ctx.ui;
  if (!ui.in_scroll) {
    NJIN_WARN("ui_scroll_end without ui_scroll_begin");
    return;
  }
  if (ui.row_cols > 0) {
    ui.cursor = row_end(ui);
    ui.row_cols = 0;
  }
  f32 &off = ui.scroll_offset[ui.scroll_id];
  const f32 content = ui.cursor + off - ui.scroll_from;
  ui.scroll_content[ui.scroll_id] = content;
  // Asked to the end: laid out this frame on last frame's height, so it lands
  // on what it holds now next frame.
  if (ui.scroll_to_end.erase(ui.scroll_id) != 0)
    off = std::max(0.0f, content - ui.scroll_area.size.y);
  ui_cmd clip{};
  clip.kind = ui_cmd::clip_end;
  ui.cmds.push_back(std::move(clip));
  // The bar over what it holds, when it holds more than shows.
  if (ui.style.scrollbar_width > 0.0f && content > ui.scroll_area.size.y) {
    const rect rail = scroll_rail(ui, ui.scroll_area);
    if (ui.style.scrollbar_rail.color.a > 0.0f) {
      ui_cmd c{};
      c.kind = ui_cmd::skin;
      c.area = rail;
      c.look = ui.style.scrollbar_rail;
      ui.cmds.push_back(std::move(c));
    }
    const widget_state st = ui.scroll_drag == ui.scroll_id ? state_pressed
                            : ui.scroll_hover              ? state_focused
                                                           : state_normal;
    push_skin(ui, ui.style.scrollbar, st, scroll_thumb(ui, ui.scroll_area, off, content));
  }
  ui.scroll_reserve = 0.0f;
  ui.scrolls.push_back(ui.scroll_area);
  ui.scroll_panel.push_back(ui.panel_id);
  ui.in_scroll = false;
  ui.cursor = ui.scroll_area.pos.y + ui.scroll_area.size.y + sc(ui, ui.style.spacing);
  ui.last_rect = ui.scroll_area;
}

void ui_scroll_to_end(context &ctx, const char *id) {
  ui_state &ui = ctx.ui;
  require_panel(ui, "ui_scroll_to_end");
  ui.scroll_to_end.insert(hash_id(ui.panel_id, id != nullptr ? id : "scroll"));
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

bool ui_input(context &ctx, const char *label, std::string &text, const ui_input_desc &desc) {
  ui_state &ui = ctx.ui;
  require_panel(ui, "ui_input");
  if (label == nullptr)
    return false;
  const interaction it = interact(ctx, label, true, false);
  bool changed = false;
  if (ui.editing == it.id) {
    ui.edit_drawn = true;
    for (i32 k = 0; k < ui.edit_back && !text.empty(); k++) {
      text.erase(utf8_last(text));
      changed = true;
    }
    if (!ui.edit_typed.empty()) {
      // Character by character, up to the most it holds.
      for (usize at = 0; at < ui.edit_typed.size();) {
        usize next = at + 1;
        while (next < ui.edit_typed.size() && ((u8)ui.edit_typed[next] & 0xC0) == 0x80)
          next++;
        if (desc.max_chars > 0 && utf8_count(text) >= desc.max_chars)
          break;
        text.append(ui.edit_typed, at, next - at);
        changed = true;
        at = next;
      }
    }
    if (ui.edit_enter || ui.edit_escape || (ui.mouse_pressed && !it.hover)) {
      ui.editing = 0;
      play(ctx, ui.edit_escape ? ui.style.sound_back : ui.style.sound_accept);
    }
  } else if (it.clicked) {
    ui.editing = it.id;
    ui.edit_drawn = true;
    ui.listening = 0;
    play(ctx, ui.style.sound_accept);
  }
  const bool on = ui.editing == it.id;
  push_skin(ui, ui.style.track, on ? state_pressed : it.state, it.area);
  const f32 room = it.area.size.x - sc(ui, 28.0f);
  if (text.empty() && !on) {
    const std::string hint = desc.hint != nullptr ? std::string(desc.hint) : shown(label);
    text_left(ctx, ui, hint, it.area, ui.style.track.text_disabled);
  } else {
    // The end of the text, with the caret, as much as fits: whole characters
    // dropped from the front.
    std::string view = text;
    const bool caret = on && std::fmod(GetTime(), 1.0) < 0.55; // real time: it blinks when paused
    while (!view.empty() && measure(ctx, ui, view + "|").x > room) {
      usize n = 1;
      while (n < view.size() && ((u8)view[n] & 0xC0) == 0x80)
        n++;
      view.erase(0, n);
    }
    text_left(ctx, ui, caret ? view + "|" : view, it.area, text_for(ui.style.track, on ? state_focused : it.state));
  }
  return changed;
}

bool ui_typing(const context &ctx) { return ctx.ui.editing != 0; }

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

rect ui_panel_rect(const context &ctx) { return ctx.ui.panel_done; }

rect ui_panel_rect(const context &ctx, const char *id) {
  if (id == nullptr)
    return {};
  const u64 h = hash_id(0, id);
  for (size_t i = ctx.ui.last_panel_ids.size(); i-- > 0;)
    if (ctx.ui.last_panel_ids[i] == h)
      return ctx.ui.last_panels[i];
  return {};
}

void ui_let_through(context &ctx, rect area) {
  ctx.ui.through.push_back(area);
  ctx.ui.through_panel.push_back(ctx.ui.in_panel ? ctx.ui.panel_id : 0);
}

bool ui_mouse_over(const context &ctx) { return ctx.ui.mouse_over; }

bool ui_on_top(const context &ctx, const char *id) {
  return id != nullptr && ctx.ui.mouse_top != 0 && ctx.ui.mouse_top == hash_id(0, id);
}

bool ui_pressed_on(const context &ctx, const char *id) { return ctx.ui.mouse_pressed && ui_on_top(ctx, id); }

bool ui_covered(const context &ctx) { return ctx.ui.panel_covered; }

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

namespace {
// ui_tooltip(): a small box by the mouse, in the toast's dress, kept on screen.
void draw_tooltip(context &ctx, ui_state &ui) {
  if (ui.tooltip.empty())
    return;
  const f32 size = font_px(ui) * 0.85f;
  const f32 pad = sc(ui, 6.0f);
  const vec2 m = measure(ctx, ui, ui.tooltip, size);
  const vec2 screen = screen_size(ctx);
  const vec2 box{m.x + 2.0f * pad, m.y + 2.0f * pad};
  vec2 pos = ui.mouse + vec2{sc(ui, 16.0f), sc(ui, 18.0f)};
  pos.x = std::min(pos.x, screen.x - box.x - 2.0f);
  pos.y = std::min(pos.y, screen.y - box.y - 2.0f);
  ui_cmd back{};
  back.kind = ui_cmd::skin;
  back.area = {pos, box};
  back.look = ui.style.toast.normal;
  back.shader = ui.style.toast.shader;
  ui_cmd text{};
  text.kind = ui_cmd::text;
  text.str = ui.tooltip;
  text.size = size;
  text.area.pos = pos + vec2{pad, pad};
  text.color = ui.style.toast.text;
  flush(ctx, ui, {back, text});
  ui.tooltip.clear();
}
} // namespace

void ui_draw_toasts(context &ctx) {
  ui_state &ui = ctx.ui;
  draw_tooltip(ctx, ui);
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

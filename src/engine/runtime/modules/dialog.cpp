#include "dialog.h"
#include "njin_cfg.h"
#include "njin_ctx.h"
#include "njin_input.h"
#include "njin_render.h"
#include "njin_ctx_impl.h"
#include "njin_draw.h"
#include "njin_i18n.h"
#include "njin_log.h"
#include "ui.h"
#include <algorithm>
#include <cmath>

namespace njin {
namespace {
// A string from a script: '@key' is looked up in the string tables.
std::string resolve(const context &ctx, const std::string &s) {
  if (!s.empty() && s[0] == '@')
    return tr(ctx, s.c_str() + 1);
  return s;
}

i32 count_codepoints(const std::string &s) {
  i32 n = 0;
  for (const char c : s)
    if (((unsigned char)c & 0xC0) != 0x80)
      n++;
  return n;
}

// The first `n` codepoints of `s`.
std::string first_codepoints(const std::string &s, i32 n) {
  usize i = 0;
  i32 seen = 0;
  while (i < s.size()) {
    if (((unsigned char)s[i] & 0xC0) != 0x80) {
      if (seen == n)
        break;
      seen++;
    }
    i++;
  }
  return s.substr(0, i);
}

bool cond_ok(context &ctx, const dialog_state &d, const std::string &cond) {
  return cond.empty() || !d.condition || d.condition(ctx, cond);
}

i32 find_node(const dialog_script &script, const std::string &id) {
  for (i32 i = 0; i < (i32)script.nodes.size(); i++)
    if (script.nodes[(usize)i].id == id)
      return i;
  return -1;
}

void finish(context &ctx) {
  dialog_state &d = ctx.dialog;
  if (!d.active)
    return;
  d.active = false;
  d.node = -1;
  if (d.paused_by_us) {
    time_set_paused(ctx, false);
    d.paused_by_us = false;
  }
  events(ctx).enqueue(dialog_ended{d.last_node});
}

// Shows node `id`, skipping those whose condition is false. Empty id ends.
void go_to(context &ctx, std::string id) {
  dialog_state &d = ctx.dialog;
  for (i32 guard = 0; guard < 256; guard++) {
    if (id.empty()) {
      finish(ctx);
      return;
    }
    const i32 index = find_node(d.script, id);
    if (index < 0) {
      NJIN_WARN("dialog: no node '%s'", id.c_str());
      finish(ctx);
      return;
    }
    const dialog_node &n = d.script.nodes[(usize)index];
    if (!cond_ok(ctx, d, n.cond)) {
      id = n.next;
      continue;
    }
    d.node = index;
    d.last_node = n.id;
    d.speaker = resolve(ctx, n.speaker);
    d.text = resolve(ctx, n.text);
    d.total = count_codepoints(d.text);
    d.shown = d.style.chars_per_second > 0.0f ? 0.0f : (f32)d.total;
    d.blips = 0;
    d.wrap_width = -1.0f;
    d.selected = 0;
    d.choices.clear();
    d.choice_text.clear();
    for (i32 c = 0; c < (i32)n.choices.size(); c++) {
      if (cond_ok(ctx, d, n.choices[(usize)c].cond)) {
        d.choices.push_back(c);
        d.choice_text.push_back(resolve(ctx, n.choices[(usize)c].text));
      }
    }
    if (!n.event.empty())
      events(ctx).enqueue(dialog_event{n.event, n.id});
    return;
  }
  NJIN_WARN("dialog: too many skipped nodes, stopping");
  finish(ctx);
}

void open(context &ctx, const char *start) {
  dialog_state &d = ctx.dialog;
  const bool was_active = d.active;
  d.active = true;
  if (!was_active && d.style.pause_game && !time_paused(ctx)) {
    time_set_paused(ctx, true);
    d.paused_by_us = true;
  }
  std::string first = start != nullptr ? start : d.script.start;
  if (first.empty() && !d.script.nodes.empty())
    first = d.script.nodes.front().id;
  go_to(ctx, first);
}

void play(context &ctx, sound_handle s) {
  if (s.id != 0)
    sound_play_once(ctx, s);
}

bool any_pad(const context &ctx, gamepad_button b) {
  for (i32 p = 0; p < gamepad_max; p++)
    if (pad_available(ctx, p) && pad_pressed(ctx, p, b))
      return true;
  return false;
}

void update(context &ctx) {
  dialog_state &d = ctx.dialog;
  if (!d.active || d.node < 0)
    return;
  const dialog_node &n = d.script.nodes[(usize)d.node];
  const bool typing = d.shown < (f32)d.total;
  const bool has_choices = !typing && !d.choices.empty();

  bool advance = key_pressed(ctx, key_enter) || key_pressed(ctx, key_space) ||
                 any_pad(ctx, pad_face_down) ||
                 (d.style.advance.id != 0 && action_pressed(ctx, d.style.advance));
  const bool click = mouse_pressed(ctx, mouse_left);
  i32 move = 0;
  if (has_choices) {
    if (key_pressed(ctx, key_up) || any_pad(ctx, pad_dpad_up))
      move = -1;
    if (key_pressed(ctx, key_down) || any_pad(ctx, pad_dpad_down))
      move = 1;
    const vec2 m = mouse_pos(ctx);
    for (usize i = 0; i < d.choice_rects.size() && i < d.choices.size(); i++) {
      if (point_in_rect(m, d.choice_rects[i])) {
        if (mouse_delta(ctx) != vec2{0.0f, 0.0f})
          d.selected = (i32)i;
        if (click)
          advance = true;
      }
    }
  } else if (click) {
    advance = true;
  }
  // The dialogue owns these while it is open.
  for (const key_code k : {key_enter, key_space})
    key_consume(ctx, k);
  mouse_consume(ctx, mouse_left);
  if (has_choices)
    for (const key_code k : {key_up, key_down})
      key_consume(ctx, k);

  if (typing) {
    d.shown = std::min(d.shown + d.style.chars_per_second * ctx.time.dt_real, (f32)d.total);
    const i32 due = (i32)d.shown / 3;
    if (due > d.blips) {
      d.blips = due;
      play(ctx, d.style.sound_blip);
    }
    if (advance)
      d.shown = (f32)d.total; // first press shows the whole line
    return;
  }
  if (has_choices) {
    if (move != 0) {
      d.selected = (d.selected + move + (i32)d.choices.size()) % (i32)d.choices.size();
      play(ctx, d.style.sound_next);
    }
    if (advance) {
      const dialog_choice &c = n.choices[(usize)d.choices[(usize)d.selected]];
      play(ctx, d.style.sound_next);
      if (!c.event.empty())
        events(ctx).enqueue(dialog_event{c.event, n.id});
      go_to(ctx, c.next);
    }
    return;
  }
  if (advance) {
    play(ctx, d.style.sound_next);
    go_to(ctx, n.next);
  }
}

void setup(context &ctx) { ecs_register(ctx, phase_pre_update, update, "update"); }
} // namespace

mod_desc dialog_module() { return mod_desc{.name = "njin.dialog", .setup = setup}; }

dialog_style dialog_default_style() {
  const ui_style ui = ui_default_style();
  dialog_style s{};
  s.box = ui.panel;
  s.box.text = {0.94f, 0.95f, 1.0f, 1.0f};
  s.name.normal = {.color = {0.26f, 0.56f, 0.98f, 1.0f}, .roundness = 0.4f};
  s.name.text = {1.0f, 1.0f, 1.0f, 1.0f};
  s.choice = ui.button;
  return s;
}

void dialog_set_style(context &ctx, const dialog_style &style) { ctx.dialog.style = style; }
dialog_style dialog_get_style(const context &ctx) { return ctx.dialog.style; }

void dialog_portrait(context &ctx, const char *name, texture_handle texture, rect source) {
  if (name != nullptr)
    ctx.dialog.portraits[name] = dialog_portrait_rec{texture, source};
}

void dialog_set_condition(context &ctx, std::function<bool(context &, const std::string &)> fn) {
  ctx.dialog.condition = std::move(fn);
}

bool dialog_parse(const json_value &json, dialog_script &out) {
  out = dialog_script{};
  const json_value &nodes = json["nodes"];
  if (!nodes.is(json_value::array)) {
    NJIN_WARN("dialog: a script needs a \"nodes\" array");
    return false;
  }
  for (usize i = 0; i < nodes.items.size(); i++) {
    const json_value &j = nodes.items[i];
    dialog_node n;
    n.id = j["id"].string_or("");
    if (n.id.empty())
      n.id = "#" + std::to_string(i);
    n.speaker = j["speaker"].string_or("");
    n.text = j["text"].string_or("");
    n.portrait = j["portrait"].string_or("");
    n.event = j["event"].string_or("");
    n.cond = j["if"].string_or("");
    n.next = j["next"].string_or("");
    for (const json_value &c : j["choices"].items)
      n.choices.push_back(dialog_choice{c["text"].string_or(""), c["next"].string_or(""),
                                        c["event"].string_or(""), c["if"].string_or("")});
    out.nodes.push_back(std::move(n));
  }
  // No "next" at all (not even null) and no choices: go on to the node below.
  for (usize i = 0; i + 1 < out.nodes.size(); i++) {
    const json_value &j = nodes.items[i];
    if (!j.has("next") && out.nodes[i].choices.empty())
      out.nodes[i].next = out.nodes[i + 1].id;
  }
  out.start = json["start"].string_or("");
  return true;
}

bool dialog_load(const char *path, dialog_script &out) {
  json_value root;
  if (path == nullptr || !json_load(path, root)) {
    NJIN_WARN("dialog: cannot read %s", path != nullptr ? path : "(null)");
    return false;
  }
  return dialog_parse(root, out);
}

void dialog_start(context &ctx, const dialog_script &script, const char *start) {
  ctx.dialog.script = script;
  open(ctx, start);
}

void dialog_say(context &ctx, const char *speaker, const char *text, const char *portrait) {
  dialog_script s;
  dialog_node n;
  n.id = "say";
  n.speaker = speaker != nullptr ? speaker : "";
  n.text = text != nullptr ? text : "";
  n.portrait = portrait != nullptr ? portrait : "";
  s.nodes.push_back(std::move(n));
  dialog_start(ctx, s);
}

bool dialog_active(const context &ctx) { return ctx.dialog.active; }

void dialog_stop(context &ctx) { finish(ctx); }

void dialog_draw(context &ctx) {
  dialog_state &d = ctx.dialog;
  if (!d.active || d.node < 0)
    return;
  const dialog_style &st = d.style;
  const dialog_node &n = d.script.nodes[(usize)d.node];
  const font_handle font = st.font.id != 0 ? st.font : ui_style_get(ctx).font;
  const f32 s = st.scale;
  const f32 size = st.font_size * s;
  const f32 pad = st.padding * s;
  const f32 line_h = text_measure(ctx, "Ag", size, font).y * 1.15f;
  const vec2 screen = screen_size(ctx);
  const f32 margin = st.margin * s;
  const f32 width = std::min(screen.x - 2.0f * margin, st.max_width * s);

  const dialog_portrait_rec *portrait = nullptr;
  if (!n.portrait.empty()) {
    const auto it = d.portraits.find(n.portrait);
    if (it != d.portraits.end())
      portrait = &it->second;
  }
  const f32 text_h = line_h * (f32)std::max(st.lines, 1);
  const vec2 psize = st.portrait_size.x > 0.0f ? st.portrait_size * s : vec2{text_h, text_h};
  const f32 text_x_off = portrait != nullptr ? psize.x + pad : 0.0f;
  const f32 text_w = width - 2.0f * pad - text_x_off;
  if (d.wrap_width != text_w) {
    d.lines = text_wrap(ctx, d.text.c_str(), size, text_w, font);
    d.wrap_width = text_w;
  }
  const bool typing = d.shown < (f32)d.total;
  const bool show_choices = !typing && !d.choices.empty();
  const f32 lines_h = std::max(text_h, line_h * (f32)d.lines.size());
  const f32 choices_h = show_choices ? (line_h + 6.0f * s) * (f32)d.choices.size() + pad * 0.5f : 0.0f;
  const f32 height = std::max(lines_h, portrait != nullptr ? psize.y : 0.0f) + choices_h + 2.0f * pad;
  const vec2 pos{(screen.x - width) * 0.5f, st.top ? margin : screen.y - margin - height};

  ui_draw_look(ctx, st.box, 0, rect{pos, {width, height}});
  if (portrait != nullptr) {
    const vec2 tex = texture_size(ctx, portrait->texture);
    const rect src = portrait->source.size.x > 0.0f ? portrait->source : rect{{}, tex};
    texture_draw_ex(ctx, portrait->texture,
                    texture_draw_desc{.pos = pos + vec2{pad, pad},
                                      .source = src,
                                      .scale = {src.size.x > 0.0f ? psize.x / src.size.x : 1.0f,
                                                src.size.y > 0.0f ? psize.y / src.size.y : 1.0f}});
  }
  if (!d.speaker.empty()) {
    const vec2 m = text_measure(ctx, d.speaker.c_str(), size, font);
    const rect tag{{pos.x + pad, pos.y - m.y - pad * 0.6f}, {m.x + pad * 1.2f, m.y + pad * 0.5f}};
    ui_draw_look(ctx, st.name, 0, tag);
    draw_text(ctx, d.speaker.c_str(), tag.pos + vec2{pad * 0.6f, pad * 0.25f}, size, st.name.text, font);
  }

  // The typewriter: only the first `shown` codepoints, across the lines.
  i32 budget = (i32)d.shown;
  f32 y = pos.y + pad;
  for (const std::string &line : d.lines) {
    if (budget <= 0)
      break;
    const i32 count = count_codepoints(line);
    const std::string part = count <= budget ? line : first_codepoints(line, budget);
    draw_text(ctx, part.c_str(), {pos.x + pad + text_x_off, y}, size, st.box.text, font);
    budget -= count + 1; // the space the wrap removed
    y += line_h;
  }

  d.choice_rects.clear();
  if (show_choices) {
    f32 cy = pos.y + pad + std::max(lines_h, portrait != nullptr ? psize.y : 0.0f) + pad * 0.5f;
    for (usize i = 0; i < d.choices.size(); i++) {
      const rect r{{pos.x + pad + text_x_off, cy}, {text_w, line_h + 2.0f * s}};
      const bool focused = (i32)i == d.selected;
      ui_draw_look(ctx, st.choice, focused ? 1 : 0, r);
      const rgba color = focused ? st.choice.text_focused : st.choice.text;
      const std::string label = (focused ? "> " : "  ") + d.choice_text[i];
      draw_text(ctx, label.c_str(), r.pos + vec2{pad * 0.5f, s}, size, color, font);
      d.choice_rects.push_back(r);
      cy += line_h + 6.0f * s;
    }
  } else if (!typing) {
    // "More" marker, bobbing, bottom right.
    const f32 t = ctx.time.elapsed;
    const f32 k = size * 0.35f;
    const vec2 c{pos.x + width - pad - k, pos.y + height - pad - k + std::sin(t * 6.0f) * 2.0f * s};
    draw_triangle(ctx, c + vec2{-k, -k * 0.6f}, c + vec2{k, -k * 0.6f}, c + vec2{0.0f, k * 0.6f},
                  st.box.text);
  }
}
} // namespace njin

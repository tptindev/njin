#include "render.h"
#include "audio.h"
#include "sim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace xiangqi {

namespace {

shader_handle sh_piece{};
// sdf_ui.fs drawn by the game itself: world health bars, minimap frame, badges.
shader_handle sh_hud{};
// sdf_ui.fs as njin ui_look shaders, one program per look so each keeps its
// own mode and border uniforms.
shader_handle sh_look_panel{};
shader_handle sh_look_button{};
shader_handle sh_look_bar{};
texture_handle white_tex{};
ui_style hud_style{};

constexpr rgba col_panel_border = rgb(180, 150, 80, 220);

void draw_quad_sdf(context &ctx, rect r) {
  if (white_tex.id != 0) {
    const vec2 sz = texture_size(ctx, white_tex);
    texture_draw_ex(ctx, white_tex, texture_draw_desc{
        .pos = r.pos,
        .scale = {r.size.x / sz.x, r.size.y / sz.y},
        .tint = col_white
    });
  } else {
    draw_rect(ctx, r, col_white);
  }
}

void draw_text_centered(context &ctx, const char *str, vec2 center, f32 size, rgba col) {
  const vec2 sz = text_measure(ctx, str, size);
  draw_text(ctx, str, center - sz * 0.5f, size, col);
}

// --- SDF DRAWING HELPERS ---

void draw_sdf_piece(context &ctx, vec2 pos, piece_type type, faction side, f32 radius,
                    bool selected, bool crossed_river, f32 anim_timer, vec2 facing = {0.0f, -1.0f}) {
  const i32 idx = static_cast<i32>(type);
  const piece_spec &sp = specs[idx];

  if (sh_piece.id != 0) {
    const f32 pad = 16.0f;
    const f32 quad_sz = std::ceil((radius + pad) * 2.0f);
    const rect quad = rect_from_center(pos, {quad_sz, quad_sz});

    shader_set_vec2(ctx, sh_piece, "u_resolution", {quad_sz, quad_sz});
    shader_set_f32(ctx, sh_piece, "u_radius", radius);
    shader_set_i32(ctx, sh_piece, "u_side", static_cast<i32>(side));
    shader_set_i32(ctx, sh_piece, "u_type", idx);
    shader_set_f32(ctx, sh_piece, "u_selected", selected ? 1.0f : 0.0f);
    shader_set_f32(ctx, sh_piece, "u_crossed", crossed_river ? 1.0f : 0.0f);
    shader_set_f32(ctx, sh_piece, "u_time", anim_timer);
    shader_set_vec2(ctx, sh_piece, "u_facing", facing);

    shader_begin(ctx, sh_piece);
    draw_quad_sdf(ctx, quad);
    shader_end(ctx);
  } else {
    // Fallback if shader is unavailable
    draw_circle(ctx, pos + vec2{2.5f, 4.5f}, radius, rgb(0, 0, 0, 100));
    if (selected) {
      const f32 pulse = 0.5f + 0.5f * std::sin(anim_timer * 6.0f);
      const rgba sel_col = (side == faction::red) ? col_gold : col_white;
      draw_circle_lines(ctx, pos, radius + 4.0f + pulse * 2.0f, 2.0f, sel_col);
    }
    const rgba rim_color = (side == faction::red) ? col_red_rim : col_black_rim;
    draw_circle(ctx, pos, radius, rim_color);
    const rgba base_color = (side == faction::red) ? col_red_base : col_black_base;
    draw_circle(ctx, pos, radius - 3.2f, base_color);
  }

  // Piece Inscription (Calligraphy text label)
  const char *label = (side == faction::red) ? sp.name_red : sp.name_black;
  const rgba text_color = (side == faction::red) ? col_red_text : col_black_text;

  f32 font_size = 12.0f;
  if (type == piece_type::general)
    font_size = 14.0f;
  else if (type == piece_type::elephant || type == piece_type::chariot)
    font_size = 13.0f;
  else if (type == piece_type::cannon)
    font_size = 12.0f;
  else if (type == piece_type::pawn)
    font_size = 11.0f;

  draw_text_centered(ctx, label, pos + vec2{0.0f, -radius * 0.12f}, font_size, text_color);
}

void draw_sdf_panel(context &ctx, rect r, rgba bg_top, rgba bg_bottom, rgba border_col,
                    f32 roundness = 4.0f, f32 border_width = 1.5f, bool has_corners = true) {
  if (sh_hud.id != 0) {
    shader_set_vec2(ctx, sh_hud, "u_resolution", r.size);
    shader_set_i32(ctx, sh_hud, "u_mode", 0); // Panel
    shader_set_f32(ctx, sh_hud, "u_roundness", roundness);
    shader_set_f32(ctx, sh_hud, "u_border_width", border_width);
    shader_set_vec4(ctx, sh_hud, "u_color_bg", vec4{bg_top.r, bg_top.g, bg_top.b, bg_top.a});
    shader_set_vec4(ctx, sh_hud, "u_color_bg2", vec4{bg_bottom.r, bg_bottom.g, bg_bottom.b, bg_bottom.a});
    shader_set_vec4(ctx, sh_hud, "u_color_border", vec4{border_col.r, border_col.g, border_col.b, border_col.a});
    shader_set_f32(ctx, sh_hud, "u_has_corners", has_corners ? 1.0f : 0.0f);
    shader_set_f32(ctx, sh_hud, "u_time", elapsed(ctx));

    shader_begin(ctx, sh_hud);
    draw_quad_sdf(ctx, r);
    shader_end(ctx);
  } else {
    draw_rect(ctx, r, bg_top);
    draw_rect_lines(ctx, r, border_width, border_col);
  }
}

void draw_sdf_bar(context &ctx, rect r, f32 value, rgba bar_col, rgba bar_col2,
                  rgba border_col = col_white, f32 roundness = 3.0f, f32 border_width = 1.0f,
                  bool right_to_left = false) {
  if (sh_hud.id != 0) {
    shader_set_vec2(ctx, sh_hud, "u_resolution", r.size);
    shader_set_i32(ctx, sh_hud, "u_mode", 1); // Bar
    shader_set_f32(ctx, sh_hud, "u_roundness", roundness);
    shader_set_f32(ctx, sh_hud, "u_border_width", border_width);
    shader_set_f32(ctx, sh_hud, "u_value", value);
    shader_set_i32(ctx, sh_hud, "u_fill_dir", right_to_left ? 1 : 0);
    shader_set_vec4(ctx, sh_hud, "u_color_bg", vec4{bar_col.r, bar_col.g, bar_col.b, bar_col.a});
    shader_set_vec4(ctx, sh_hud, "u_color_bg2", vec4{bar_col2.r, bar_col2.g, bar_col2.b, bar_col2.a});
    shader_set_vec4(ctx, sh_hud, "u_color_border", vec4{border_col.r, border_col.g, border_col.b, border_col.a});
    shader_set_f32(ctx, sh_hud, "u_time", elapsed(ctx));

    shader_begin(ctx, sh_hud);
    draw_quad_sdf(ctx, r);
    shader_end(ctx);
  } else {
    draw_rect(ctx, r, rgb(20, 20, 20, 220));
    const f32 w = (r.size.x - 2.0f) * clamp(value, 0.0f, 1.0f);
    if (right_to_left) {
      draw_rect(ctx, {{r.pos.x + 1.0f + (r.size.x - 2.0f - w), r.pos.y + 1.0f}, {w, r.size.y - 2.0f}}, bar_col);
    } else {
      draw_rect(ctx, {{r.pos.x + 1.0f, r.pos.y + 1.0f}, {w, r.size.y - 2.0f}}, bar_col);
    }
    draw_rect_lines(ctx, r, border_width, border_col);
  }
}

void draw_sdf_ring(context &ctx, rect r, f32 progress, rgba bg_col, rgba border_col) {
  if (sh_hud.id != 0) {
    shader_set_vec2(ctx, sh_hud, "u_resolution", r.size);
    shader_set_i32(ctx, sh_hud, "u_mode", 3); // Ring
    shader_set_f32(ctx, sh_hud, "u_value", progress);
    shader_set_vec4(ctx, sh_hud, "u_color_bg", vec4{bg_col.r, bg_col.g, bg_col.b, bg_col.a});
    shader_set_vec4(ctx, sh_hud, "u_color_border", vec4{border_col.r, border_col.g, border_col.b, border_col.a});
    shader_set_f32(ctx, sh_hud, "u_time", elapsed(ctx));

    shader_begin(ctx, sh_hud);
    draw_quad_sdf(ctx, r);
    shader_end(ctx);
  } else {
    draw_circle(ctx, rect_center(r), std::min(r.size.x, r.size.y) * 0.5f, bg_col);
    draw_circle_lines(ctx, rect_center(r), std::min(r.size.x, r.size.y) * 0.5f, 2.0f, border_col);
  }
}

void draw_sdf_marquee(context &ctx, rect r) {
  if (sh_hud.id != 0) {
    shader_set_vec2(ctx, sh_hud, "u_resolution", r.size);
    shader_set_i32(ctx, sh_hud, "u_mode", 4); // Box select
    shader_set_f32(ctx, sh_hud, "u_time", elapsed(ctx));

    shader_begin(ctx, sh_hud);
    draw_quad_sdf(ctx, r);
    shader_end(ctx);
  } else {
    draw_rect(ctx, r, rgba{0.2f, 0.85f, 0.4f, 0.15f});
    draw_rect_lines(ctx, r, 1.2f, rgb(80, 230, 120, 220));
  }
}

void draw_sdf_badge(context &ctx, rect r, rgba bg_col, rgba border_col, bool is_diamond = false) {
  if (sh_hud.id != 0) {
    shader_set_vec2(ctx, sh_hud, "u_resolution", r.size);
    shader_set_i32(ctx, sh_hud, "u_mode", 5); // Badge
    shader_set_vec4(ctx, sh_hud, "u_color_bg", vec4{bg_col.r, bg_col.g, bg_col.b, bg_col.a});
    shader_set_vec4(ctx, sh_hud, "u_color_bg2", vec4{bg_col.r * 0.75f, bg_col.g * 0.75f, bg_col.b * 0.75f, bg_col.a});
    shader_set_vec4(ctx, sh_hud, "u_color_border", vec4{border_col.r, border_col.g, border_col.b, border_col.a});
    shader_set_f32(ctx, sh_hud, "u_has_corners", is_diamond ? 1.0f : 0.0f);
    shader_set_f32(ctx, sh_hud, "u_border_width", 1.2f);

    shader_begin(ctx, sh_hud);
    draw_quad_sdf(ctx, r);
    shader_end(ctx);
  } else {
    draw_rect(ctx, r, bg_col);
    draw_rect_lines(ctx, r, 1.0f, border_col);
  }
}

void draw_grid_and_palaces(context &ctx) {
  constexpr f32 step_x = 240.0f;
  constexpr f32 step_y = 160.0f;

  for (f32 x = step_x; x < world_width; x += step_x) {
    draw_line(ctx, {x, 60.0f}, {x, river_top}, 1.0f, col_grid_lines);
    draw_line(ctx, {x, river_bottom}, {x, world_height - 60.0f}, 1.0f, col_grid_lines);
  }

  for (f32 y = 60.0f; y <= river_top; y += step_y) {
    draw_line(ctx, {step_x, y}, {world_width - step_x, y}, 1.0f, col_grid_lines);
  }
  for (f32 y = river_bottom; y <= world_height - 60.0f; y += step_y) {
    draw_line(ctx, {step_x, y}, {world_width - step_x, y}, 1.0f, col_grid_lines);
  }

  // The Nine Palaces (Cửu Cung) Diagonal Lines
  const rect p_north{{960.0f, 60.0f}, {480.0f, 160.0f}};
  draw_rect_lines(ctx, p_north, 1.5f, rgb(70, 160, 150, 120));
  draw_line(ctx, {960.0f, 60.0f}, {1440.0f, 220.0f}, 1.2f, rgb(70, 160, 150, 100));
  draw_line(ctx, {1440.0f, 60.0f}, {960.0f, 220.0f}, 1.2f, rgb(70, 160, 150, 100));

  const rect p_south{{960.0f, 1380.0f}, {480.0f, 160.0f}};
  draw_rect_lines(ctx, p_south, 1.5f, rgb(210, 80, 70, 120));
  draw_line(ctx, {960.0f, 1380.0f}, {1440.0f, 1540.0f}, 1.2f, rgb(210, 80, 70, 100));
  draw_line(ctx, {1440.0f, 1380.0f}, {960.0f, 1540.0f}, 1.2f, rgb(210, 80, 70, 100));

  draw_text_centered(ctx, "CỬU CUNG BẮC QUÂN", {1200.0f, 85.0f}, 13.0f, rgb(80, 180, 170, 130));
  draw_text_centered(ctx, "CỬU CUNG NAM QUÂN", {1200.0f, 1515.0f}, 13.0f, rgb(220, 90, 80, 130));
}

void draw_river_and_bridges(context &ctx) {
  const f32 time = elapsed(ctx);

  draw_rect(ctx, {{0.0f, river_top}, {world_width, river_bottom - river_top}}, col_river_deep);

  for (f32 x = 0.0f; x < world_width; x += 180.0f) {
    const f32 wave_offset = std::sin(time * 2.0f + x * 0.02f) * 12.0f;
    const f32 y1 = river_top + 40.0f + wave_offset;
    const f32 y2 = river_top + 90.0f - wave_offset;
    draw_line(ctx, {x, y1}, {x + 140.0f, y1 + 5.0f}, 2.0f, col_river_shallow);
    draw_line(ctx, {x + 30.0f, y2}, {x + 160.0f, y2 - 4.0f}, 1.5f, col_water_foam);
  }

  draw_line(ctx, {0.0f, river_top}, {world_width, river_top}, 3.0f, rgb(90, 80, 60));
  draw_line(ctx, {0.0f, river_bottom}, {world_width, river_bottom}, 3.0f, rgb(90, 80, 60));

  draw_text_centered(ctx, "SỞ HÀ (NAM QUÂN)", {680.0f, river_center_y}, 16.0f, rgb(230, 240, 255, 120));
  draw_text_centered(ctx, "--- SỞ HÀ HÁN GIỚI ---", {1200.0f, river_center_y}, 18.0f, rgb(255, 230, 160, 160));
  draw_text_centered(ctx, "HÁN GIỚI (BẮC QUÂN)", {1720.0f, river_center_y}, 16.0f, rgb(230, 240, 255, 120));

  for (const auto &b : bridges) {
    draw_rect(ctx, {{b.area.pos.x + 5.0f, b.area.pos.y + 4.0f}, b.area.size}, rgb(0, 0, 0, 110));
    draw_rect(ctx, b.area, col_bridge_wood);
    draw_rect_lines(ctx, b.area, 3.0f, col_bridge_light);

    for (f32 by = b.area.pos.y + 12.0f; by < b.area.pos.y + b.area.size.y - 8.0f; by += 16.0f) {
      draw_line(ctx, {b.area.pos.x + 4.0f, by}, {b.area.pos.x + b.area.size.x - 4.0f, by}, 1.0f, rgb(95, 65, 40));
    }

    draw_line(ctx, {b.area.pos.x, b.area.pos.y}, {b.area.pos.x, b.area.pos.y + b.area.size.y}, 3.0f, col_bridge_light);
    draw_line(ctx, {b.area.pos.x + b.area.size.x, b.area.pos.y},
              {b.area.pos.x + b.area.size.x, b.area.pos.y + b.area.size.y}, 3.0f, col_bridge_light);

    draw_text_centered(ctx, b.name, {b.area.pos.x + b.area.size.x * 0.5f, b.area.pos.y + b.area.size.y * 0.5f}, 12.0f,
                       rgb(240, 230, 210, 220));
  }
}

void draw_outposts(context &ctx) {
  const auto &reg = world(ctx);
  for (const auto [oe, op] : reg.view<const outpost_component>().each()) {
    rgba ring_col = col_muted;
    rgba ring_dark = rgb(40, 45, 50);
    if (op.owner == 0) {
      ring_col = col_red_rim;
      ring_dark = rgb(130, 25, 20);
    } else if (op.owner == 1) {
      ring_col = col_black_rim;
      ring_dark = rgb(15, 80, 75);
    }

    draw_circle_lines(ctx, op.pos, op.radius, 1.5f, ring_col);
    draw_circle(ctx, op.pos, op.radius, rgba{ring_col.r, ring_col.g, ring_col.b, 0.08f});

    // Central Pillar / Tower base
    draw_circle(ctx, op.pos + vec2{2.0f, 3.0f}, 18.0f, rgb(0, 0, 0, 100));
    draw_circle(ctx, op.pos, 18.0f, rgb(70, 75, 80));
    draw_circle(ctx, op.pos, 14.0f, ring_col);
    draw_circle_lines(ctx, op.pos, 18.0f, 2.0f, col_white);

    // Flag pole & banner
    const vec2 pole_top = op.pos + vec2{0.0f, -32.0f};
    draw_line(ctx, op.pos, pole_top, 2.5f, col_white);
    draw_triangle(ctx, pole_top, pole_top + vec2{22.0f, 7.0f}, pole_top + vec2{0.0f, 15.0f}, ring_col);

    // Outpost name
    draw_text_centered(ctx, op.name, op.pos + vec2{0.0f, -42.0f}, 12.0f, col_white);

    // SDF Capture Progress Bar (-100 to +100)
    const vec2 bar_pos = op.pos + vec2{-26.0f, 25.0f};
    const f32 progress = clamp((op.control + 100.0f) / 200.0f, 0.0f, 1.0f);
    draw_sdf_bar(ctx, {bar_pos, {52.0f, 7.0f}}, progress, ring_col, ring_dark, col_white, 2.5f, 1.0f);
  }
}

// --- HUD ON njin UI ---

// Loads sdf_ui.fs as a njin ui_look shader in `mode` (0 panel, 1 bar, 2 button).
// The skin colour reaches the shader as fragColor; the rest is fixed per look.
shader_handle load_look_shader(context &ctx, i32 mode, f32 roundness, f32 border_width, rgba border,
                               f32 shade, bool corners) {
  const shader_handle sh = shader_load(ctx, nullptr, "assets/shaders/sdf_ui.fs");
  if (sh.id == 0)
    return sh;
  shader_set_i32(ctx, sh, "u_ui", 1);
  shader_set_i32(ctx, sh, "u_mode", mode);
  shader_set_i32(ctx, sh, "u_fill_dir", 0);
  shader_set_f32(ctx, sh, "u_roundness", roundness);
  shader_set_f32(ctx, sh, "u_border_width", border_width);
  shader_set_vec4(ctx, sh, "u_color_border", vec4{border.r, border.g, border.b, border.a});
  shader_set_f32(ctx, sh, "u_shade", shade);
  shader_set_f32(ctx, sh, "u_has_corners", corners ? 1.0f : 0.0f);
  return sh;
}

// A face drawn by the SDF shader over a white quad: the shader needs 0..1
// texture coordinates to know where it is in the widget.
ui_skin sdf_skin(rgba color) {
  return ui_skin{.texture = white_tex, .color = color};
}

ui_style make_hud_style() {
  ui_style s = ui_default_style();
  s.font_size = 14.0f;
  s.padding = 10.0f;
  s.spacing = 6.0f;
  s.widget_height = 30.0f;
  s.dim = rgb(0, 0, 0, 190);

  s.panel.normal = sdf_skin(rgb(24, 29, 36, 250));
  s.panel.shader = sh_look_panel;
  s.panel.text = col_gold;
  s.label.text = s.label.text_focused = col_white;

  s.button.normal = sdf_skin(rgb(24, 30, 38));
  s.button.focused = sdf_skin(rgb(36, 46, 56));
  s.button.pressed = sdf_skin(rgb(36, 46, 56));
  s.button.disabled = sdf_skin(rgb(24, 30, 38));
  s.button.shader = sh_look_button;
  s.button.text = col_white;
  s.button.text_focused = col_gold_light;
  s.button.text_disabled = col_muted;

  // The bar shader draws the whole bar, slot and fill, from uiValue; the skin
  // colour is the fill colour. So the fill face draws nothing.
  const ui_skin bar = sdf_skin(col_health_green);
  s.track.normal = s.track.focused = s.track.pressed = s.track.disabled = bar;
  s.track.shader = sh_look_bar;
  s.track.text = s.track.text_focused = col_white;
  const ui_skin none{.color = {0.0f, 0.0f, 0.0f, 0.0f}};
  s.fill.normal = s.fill.focused = s.fill.pressed = s.fill.disabled = none;

  s.sound_move = {};
  s.sound_accept = audio_sound(sfx_type::click);
  return s;
}

// ui_label in its own colour.
void hud_label(context &ctx, const char *text, rgba color) {
  ui_style s = ui_style_get(ctx);
  const rgba old = s.label.text;
  s.label.text = color;
  ui_style_set(ctx, s);
  ui_label(ctx, text);
  s.label.text = old;
  ui_style_set(ctx, s);
}

// ui_progress with its own fill colour.
void hud_bar(context &ctx, f32 value, const char *text, rgba color) {
  ui_style s = ui_style_get(ctx);
  const ui_look old = s.track;
  s.track.normal.color = color;
  ui_style_set(ctx, s);
  ui_progress(ctx, value, text);
  s.track = old;
  ui_style_set(ctx, s);
}

f32 hp_ratio(const entt::registry &reg, entt::entity e) {
  if (e == entt::null || !reg.valid(e))
    return 0.0f;
  const auto &u = reg.get<unit_component>(e);
  return u.max_hp > 0.0f ? clamp(u.hp / u.max_hp, 0.0f, 1.0f) : 0.0f;
}

} // namespace

void render_init(context &ctx) {
  sh_piece = shader_load(ctx, nullptr, "assets/shaders/sdf_piece.fs");
  sh_hud = shader_load(ctx, nullptr, "assets/shaders/sdf_ui.fs");
  white_tex = texture_load(ctx, "assets/white.png");

  sh_look_panel = load_look_shader(ctx, 0, 6.0f, 2.0f, col_panel_border, 0.6f, true);
  sh_look_button = load_look_shader(ctx, 2, 4.0f, 1.2f, rgb(85, 105, 120), 0.75f, false);
  sh_look_bar = load_look_shader(ctx, 1, 4.0f, 1.5f, col_gold, 0.55f, false);
  hud_style = make_hud_style();
}

void render_cleanup(context &ctx) {
  for (shader_handle *sh : {&sh_piece, &sh_hud, &sh_look_panel, &sh_look_button, &sh_look_bar}) {
    if (sh->id != 0) {
      shader_unload(ctx, *sh);
      *sh = {};
    }
  }
  if (white_tex.id != 0) {
    texture_unload(ctx, white_tex);
    white_tex = {};
  }
}

void render_world(context &ctx) {
  // 1. Terrain Grass Background
  draw_rect(ctx, {{0.0f, 0.0f}, {world_width, world_height}}, col_ground);

  // Decorative border
  draw_rect_lines(ctx, {{15.0f, 15.0f}, {world_width - 30.0f, world_height - 30.0f}}, 3.0f, rgb(80, 95, 85));

  // 2. Xiangqi Grid & Cửu Cung Palaces
  draw_grid_and_palaces(ctx);

  // 3. Central River & Bridges
  draw_river_and_bridges(ctx);

  // 4. Outposts
  draw_outposts(ctx);

  // 5. Click Movement Pings
  for (const auto &p : state.pings) {
    const f32 progress = p.timer / p.max_time;
    const f32 radius = 10.0f + progress * 24.0f;
    const f32 alpha = 1.0f - progress;
    draw_circle_lines(ctx, p.pos, radius, 2.0f, rgba{p.color.r, p.color.g, p.color.b, alpha});
  }

  // 6. Units (Rendered via SDF Piece Shader)
  const auto &reg = world(ctx);
  const auto unit_view = reg.view<const transform, const unit_component>();

  for (const auto [e, tr, u] : unit_view.each()) {
    draw_sdf_piece(ctx, tr.pos, u.type, u.side, u.radius, u.selected, u.crossed_river, u.anim_timer, u.facing);

    // Health Bar (Rendered via SDF Bar)
    const f32 bar_w = u.radius * 2.2f;
    const f32 bar_h = 5.0f;
    const vec2 bar_pos = tr.pos + vec2{-bar_w * 0.5f, -u.radius - 12.0f};

    const f32 ratio = clamp(u.hp / (u.max_hp > 0.0f ? u.max_hp : 1.0f), 0.0f, 1.0f);
    rgba hp_col = col_health_green;
    rgba hp_col2 = rgb(45, 145, 75);
    if (ratio < 0.35f) {
      hp_col = col_health_red;
      hp_col2 = rgb(150, 30, 25);
    } else if (ratio < 0.65f) {
      hp_col = col_health_yellow;
      hp_col2 = rgb(175, 135, 30);
    }

    draw_sdf_bar(ctx, {bar_pos, {bar_w, bar_h}}, ratio, hp_col, hp_col2, rgb(220, 220, 220, 160), 2.2f, 0.8f);

    // Advisor Shield Aura ring if nearby allies
    if (u.type == piece_type::advisor) {
      draw_circle_lines(ctx, tr.pos, 150.0f, 1.0f, rgb(180, 140, 240, 60));
    }
    // General Long Uy Aura ring
    if (u.type == piece_type::general) {
      draw_circle_lines(ctx, tr.pos, 220.0f, 1.2f, rgb(255, 215, 60, 75));
    }
  }

  // 7. Projectiles (Cannon Mortar Shells)
  for (const auto [pe, p] : reg.view<const projectile_component>().each()) {
    draw_circle(ctx, lerp(p.start_pos, p.target_pos, p.progress), 3.5f, rgb(0, 0, 0, 80));
    draw_circle(ctx, p.pos, 5.0f, p.color);
    draw_circle(ctx, p.pos, 2.5f, col_white);
  }

  // 8. Particles
  for (const auto &pt : state.particles) {
    const f32 alpha = clamp(pt.life / (pt.max_life > 0.0f ? pt.max_life : 1.0f), 0.0f, 1.0f);
    const rgba col{pt.color.r, pt.color.g, pt.color.b, alpha};
    if (pt.is_ring) {
      draw_circle_lines(ctx, pt.pos, pt.size * (1.0f - alpha * 0.5f), 2.0f, col);
    } else {
      draw_circle(ctx, pt.pos, pt.size * alpha, col);
    }
  }

  // 9. Floating Damage & Combat Numbers
  for (const auto &pop : state.popups) {
    const f32 alpha = clamp(pop.timer / (pop.max_time > 0.0f ? pop.max_time : 1.0f), 0.0f, 1.0f);
    const rgba col{pop.color.r, pop.color.g, pop.color.b, alpha};
    const f32 fsz = pop.is_crit ? 15.0f : 12.0f;
    draw_text_centered(ctx, pop.label.c_str(), pop.pos, fsz, col);
  }
}

void render_ui(context &ctx) {
  const vec2 scr = screen_size(ctx);
  const auto &reg = world(ctx);
  ui_style_set(ctx, hud_style);

  // --- 1. TOP BAR: resources, the two generals, match time ---
  ui_begin(ctx, {.id = "top_bar", .anchor = {0.5f, 0.0f}, .pivot = {0.5f, 0.0f}, .width = scr.x,
                 .navigable = false});
  ui_row(ctx, 5);
  char gold_buf[48];
  std::snprintf(gold_buf, sizeof(gold_buf), "Lương Thảo: %d", state.red_gold);
  hud_label(ctx, gold_buf, col_gold);
  char pop_buf[48];
  std::snprintf(pop_buf, sizeof(pop_buf), "Quân Số: %d / %d", state.red_pop, game_state::max_pop);
  hud_label(ctx, pop_buf, (state.red_pop >= game_state::max_pop) ? col_health_red : col_white);
  hud_bar(ctx, hp_ratio(reg, state.red_general), "TƯỚNG", col_red_rim);
  hud_bar(ctx, hp_ratio(reg, state.black_general), "SOÁI", col_black_rim);
  const i32 mins = static_cast<i32>(state.match_time) / 60;
  const i32 secs = static_cast<i32>(state.match_time) % 60;
  char time_buf[32];
  std::snprintf(time_buf, sizeof(time_buf), "Thời Gian: %02d:%02d", mins, secs);
  hud_label(ctx, time_buf, col_white);
  ui_end(ctx);

  // --- 2. BOTTOM LEFT: MINIMAP (drawn by the game, it is a view of the world) ---
  const rect mm_area = minimap_area;
  draw_sdf_panel(ctx, mm_area, rgb(20, 26, 34, 250), rgb(12, 16, 22, 255), rgb(80, 100, 115), 6.0f, 2.0f, true);
  draw_text(ctx, "BẢN ĐỒ CHIẾN LƯỢC", {mm_area.pos.x + 8.0f, mm_area.pos.y + 6.0f}, 11.0f, col_gold);

  const f32 mm_river_y = mm_area.pos.y + (river_top / world_height) * mm_area.size.y;
  const f32 mm_river_h = ((river_bottom - river_top) / world_height) * mm_area.size.y;
  draw_rect(ctx, {{mm_area.pos.x, mm_river_y}, {mm_area.size.x, mm_river_h}}, col_river_deep);

  const auto to_minimap = [&](vec2 p) {
    return mm_area.pos + vec2{(p.x / world_width) * mm_area.size.x, (p.y / world_height) * mm_area.size.y};
  };
  for (const auto [oe, op] : reg.view<const outpost_component>().each()) {
    rgba op_col = col_muted;
    if (op.owner == 0)
      op_col = col_red_rim;
    else if (op.owner == 1)
      op_col = col_black_rim;
    draw_circle(ctx, to_minimap(op.pos), 3.5f, op_col);
  }
  for (const auto [e, tr, u] : reg.view<const transform, const unit_component>().each()) {
    const rgba dot_col = (u.side == faction::red) ? col_red_rim : col_black_text;
    const f32 dot_r = (u.type == piece_type::general) ? 3.0f : 1.8f;
    draw_circle(ctx, to_minimap(tr.pos), dot_r, dot_col);
  }
  const f32 cam_box_w = (scr.x / state.camera_zoom / world_width) * mm_area.size.x;
  const f32 cam_box_h = (scr.y / state.camera_zoom / world_height) * mm_area.size.y;
  draw_rect_lines(ctx, rect_from_center(to_minimap(state.camera_pos), {cam_box_w, cam_box_h}), 1.2f, col_white);

  // --- 3. BOTTOM CENTER: SELECTED UNIT CARD ---
  entt::entity sel_e = entt::null;
  for (const auto [e, u] : reg.view<const unit_component>().each()) {
    if (u.side == faction::red && u.selected) {
      sel_e = e;
      break;
    }
  }

  ui_begin(ctx, {.id = "unit_card", .anchor = {0.0f, 1.0f}, .pivot = {0.0f, 1.0f}, .offset = {240.0f, -15.0f},
                 .width = 470.0f, .navigable = false});
  rect portrait_row{};
  if (sel_e != entt::null) {
    const auto &su = reg.get<unit_component>(sel_e);
    const piece_spec &ssp = specs[static_cast<i32>(su.type)];

    char title_buf[64];
    std::snprintf(title_buf, sizeof(title_buf), "%s • %s", ssp.name_red, ssp.title);
    hud_label(ctx, title_buf, col_gold);
    portrait_row = ui_last_rect(ctx);

    f32 cur_dmg = ssp.attack_damage;
    f32 cur_spd = ssp.move_speed;
    if (su.type == piece_type::pawn && su.crossed_river) {
      cur_dmg *= 1.5f;
      cur_spd *= 1.35f;
    }
    char stats_buf[128];
    std::snprintf(stats_buf, sizeof(stats_buf), "Máu: %.0f / %.0f  |  Công: %.0f  |  Tốc: %.0f  |  Tầm: %.0f",
                  su.hp, su.max_hp, cur_dmg, cur_spd, ssp.attack_range);
    hud_label(ctx, stats_buf, col_white);
    ui_space(ctx, 2.0f);

    // Leave room on the right for the portrait.
    const ui_style st = ui_style_get(ctx);
    const f32 wrap_w = portrait_row.size.x - 70.0f;
    for (const std::string &line : text_wrap(ctx, ssp.desc, st.font_size * st.scale, wrap_w, st.font))
      hud_label(ctx, line.c_str(), rgb(215, 225, 235));
  } else {
    hud_label(ctx, "QUÂN ĐOÀN KHỞI NGHĨA", col_gold);
    hud_label(ctx, "• Kéo chuột trái để chọn quân hoặc nhấp vào quân cờ", col_white);
    hud_label(ctx, "• Nhấp chuột phải để di chuyển hoặc ra lệnh tấn công", col_white);
    hud_label(ctx, "• Phím 1-6 chiêu mộ quân • R hiệu lệnh Tướng • H dừng quân", rgb(180, 220, 240));
    hud_label(ctx, "• Bảo vệ Tướng, tiêu diệt Soái đối phương để thắng!", col_gold_light);
  }
  ui_end(ctx);

  if (sel_e != entt::null) {
    const auto &su = reg.get<unit_component>(sel_e);
    const vec2 port_pos{portrait_row.pos.x + portrait_row.size.x - 30.0f, portrait_row.pos.y + 32.0f};
    draw_sdf_piece(ctx, port_pos, su.type, su.side, 24.0f, true, su.crossed_river, su.anim_timer, su.facing);
  }

  // --- 4. BOTTOM RIGHT: RECRUITMENT DOCK ---
  const piece_type recruit_types[6] = {piece_type::pawn,    piece_type::horse,    piece_type::cannon,
                                       piece_type::chariot, piece_type::elephant, piece_type::advisor};
  const char *recruit_ids[6] = {"##recruit_pawn",    "##recruit_horse",    "##recruit_cannon",
                                "##recruit_chariot", "##recruit_elephant", "##recruit_advisor"};
  rect recruit_rects[6]{};
  bool can_afford[6]{};
  const bool rally_ready = state.rally_cooldown <= 0.0f;

  ui_begin(ctx, {.id = "dock", .title = "CHIÊU MỘ BINH LÍNH & KỸ NĂNG", .anchor = {1.0f, 1.0f},
                 .pivot = {1.0f, 1.0f}, .offset = {-15.0f, -15.0f}, .width = 540.0f, .navigable = false});
  ui_style tall = ui_style_get(ctx);
  tall.widget_height = 108.0f;
  ui_style_set(ctx, tall);
  ui_row(ctx, 7);
  for (i32 i = 0; i < 6; ++i) {
    const piece_spec &sp = specs[static_cast<i32>(recruit_types[i])];
    can_afford[i] = state.red_gold >= sp.cost && state.red_pop < game_state::max_pop;
    if (ui_button(ctx, recruit_ids[i], can_afford[i]))
      recruit_unit(ctx, recruit_types[i]);
    recruit_rects[i] = ui_last_rect(ctx);
  }
  if (ui_button(ctx, "##rally", rally_ready))
    trigger_rally(ctx);
  const rect rally_rect = ui_last_rect(ctx);
  ui_style_set(ctx, hud_style);
  ui_end(ctx);

  // What sits on the buttons: hotkey, piece, cost, name.
  const char *hotkeys[6] = {"1", "2", "3", "4", "5", "6"};
  for (i32 i = 0; i < 6; ++i) {
    const piece_spec &sp = specs[static_cast<i32>(recruit_types[i])];
    const rect r = recruit_rects[i];
    const f32 cx = r.pos.x + r.size.x * 0.5f;
    draw_sdf_badge(ctx, rect_from_center({cx, r.pos.y + 12.0f}, {24.0f, 14.0f}), rgb(30, 36, 44),
                   can_afford[i] ? col_gold : col_muted, false);
    draw_text_centered(ctx, hotkeys[i], {cx, r.pos.y + 12.0f}, 11.0f, can_afford[i] ? col_gold : col_muted);
    draw_sdf_piece(ctx, {cx, r.pos.y + 44.0f}, recruit_types[i], faction::red, 16.0f, false, false, 0.0f);
    char cost_buf[16];
    std::snprintf(cost_buf, sizeof(cost_buf), "%d", sp.cost);
    draw_text_centered(ctx, cost_buf, {cx, r.pos.y + 76.0f}, 11.0f, can_afford[i] ? col_gold : col_health_red);
    draw_text_centered(ctx, sp.name_red, {cx, r.pos.y + 94.0f}, 11.0f, can_afford[i] ? col_white : col_muted);
  }

  // Rally: hotkey, medallion with the cooldown sweep, status.
  const f32 rcx = rally_rect.pos.x + rally_rect.size.x * 0.5f;
  const f32 rally_progress =
      rally_ready ? 1.0f : clamp(1.0f - (state.rally_cooldown / game_state::rally_cooldown_max), 0.0f, 0.999f);
  draw_sdf_badge(ctx, rect_from_center({rcx, rally_rect.pos.y + 12.0f}, {30.0f, 15.0f}), rgb(35, 30, 20), col_gold,
                 false);
  draw_text_centered(ctx, "R", {rcx, rally_rect.pos.y + 12.0f}, 11.0f, col_gold);
  draw_sdf_ring(ctx, rect_from_center({rcx, rally_rect.pos.y + 46.0f}, {40.0f, 40.0f}), rally_progress,
                rally_ready ? rgb(85, 70, 25) : rgb(35, 32, 25), col_gold);
  draw_text_centered(ctx, "LỆNH", {rcx, rally_rect.pos.y + 46.0f}, 12.0f,
                     rally_ready ? rgb(255, 235, 140) : rgb(160, 150, 130));
  if (rally_ready) {
    draw_text_centered(ctx, "SẴN SÀNG", {rcx, rally_rect.pos.y + 80.0f}, 10.0f, col_health_green);
    draw_text_centered(ctx, "Tổng Công", {rcx, rally_rect.pos.y + 95.0f}, 10.0f, col_white);
  } else {
    char cd_buf[16];
    std::snprintf(cd_buf, sizeof(cd_buf), "%.0fs", state.rally_cooldown);
    draw_text_centered(ctx, cd_buf, {rcx, rally_rect.pos.y + 80.0f}, 12.0f, col_gold);
    draw_text_centered(ctx, "Hồi chiêu", {rcx, rally_rect.pos.y + 95.0f}, 10.0f, col_muted);
  }

  // --- 5. BOX SELECTION RECTANGLE ---
  if (state.is_box_selecting) {
    const vec2 p1 = state.box_start_screen;
    const vec2 p2 = state.box_end_screen;
    const vec2 lo{std::min(p1.x, p2.x), std::min(p1.y, p2.y)};
    const vec2 hi{std::max(p1.x, p2.x), std::max(p1.y, p2.y)};
    draw_sdf_marquee(ctx, rect{lo, hi - lo});
  }

  // --- 6. VICTORY / DEFEAT: a njin popup, so Enter, Space and the gamepad work too ---
  if (state.screen != game_screen::playing) {
    const bool won = state.screen == game_screen::victory;
    const rgba accent = won ? col_gold : col_red_rim;
    ui_style result = hud_style;
    result.panel.text = accent;
    ui_style_set(ctx, result);
    shader_set_vec4(ctx, sh_look_panel, "u_color_border", vec4{accent.r, accent.g, accent.b, accent.a});

    ui_popup_begin(ctx, {.id = "result", .title = won ? "ĐẠI THẮNG QUANG VINH" : "THỐNG SOÁI TỬ TRẬN",
                         .width = 520.0f});
    hud_label(ctx, won ? "BẮC QUÂN DIỆT VONG • THIÊN HẠ ĐỊNH PHẦN" : "QUÂN TA VỠ TRẬN • ĐẠI BẠI TIÊN PHONG",
              col_white);
    ui_space(ctx, 8.0f);
    char stat_buf1[64];
    std::snprintf(stat_buf1, sizeof(stat_buf1), "Thời gian chiến trận: %02d:%02d", mins, secs);
    hud_label(ctx, stat_buf1, rgb(220, 230, 240));
    char stat_buf2[80];
    std::snprintf(stat_buf2, sizeof(stat_buf2), "Quân địch tiêu diệt: %d   |   Tổn thất quân ta: %d",
                  state.red_kills, state.black_kills);
    hud_label(ctx, stat_buf2, rgb(220, 230, 240));
    ui_space(ctx, 8.0f);
    if (ui_button(ctx, "CHƠI LẠI"))
      state.restart_requested = true;
    ui_popup_end(ctx);

    shader_set_vec4(ctx, sh_look_panel, "u_color_border",
                    vec4{col_panel_border.r, col_panel_border.g, col_panel_border.b, col_panel_border.a});
    ui_style_set(ctx, hud_style);
  }
}

} // namespace xiangqi

#include "game.h"

#include <algorithm>
#include <cstdio>

namespace defense {
bool inside(rect r, vec2 p) {
  return p.x >= r.pos.x && p.y >= r.pos.y && p.x <= r.pos.x + r.size.x && p.y <= r.pos.y + r.size.y;
}

void text(njin_ctx &ctx, const char *value, f32 x, f32 y, f32 size, rgba color) {
  draw_text(ctx, value, {x, y}, size, color);
}

void draw_road(njin_ctx &ctx) {
  draw_rect(ctx, field, field_green);
  // Quiet ground marks give the field texture without competing with the route.
  for (i32 y = 0; y < 12; ++y) {
    for (i32 x = 0; x < 18; ++x) {
      if ((x * 7 + y * 11) % 5 == 0) {
        const f32 px = field_x + 18.0f + x * 49.0f + (y % 2) * 13.0f;
        const f32 py = field_y + 17.0f + y * 47.0f;
        draw_circle(ctx, {px, py}, 2.0f + static_cast<f32>((x + 2 * y) % 3),
                    ((x + y) % 2 == 0) ? rgba{0.82f, 0.93f, 0.73f, 0.07f} : rgba{0.06f, 0.19f, 0.16f, 0.1f});
      }
    }
  }
  for (f32 x = field_x + 26.0f; x < field_x + field_w; x += grid_step)
    draw_line(ctx, {x, field_y}, {x, field_y + field_h}, 1.0f, rgba{0.82f, 0.93f, 0.84f, 0.055f});
  for (f32 y = field_y + 26.0f; y < field_y + field_h; y += grid_step)
    draw_line(ctx, {field_x, y}, {field_x + field_w, y}, 1.0f, rgba{0.82f, 0.93f, 0.84f, 0.055f});

  for (usize i = 1; i < path.size(); ++i) {
    draw_line(ctx, path[i - 1], path[i], road_width + 8.0f, road_dark);
    draw_line(ctx, path[i - 1], path[i], road_width, road);
    draw_line(ctx, path[i - 1], path[i], road_width - 10.0f, road_light);
    const vec2 delta_path = path[i] - path[i - 1];
    const f32 len = length(delta_path);
    const vec2 dir = normalize(delta_path);
    for (f32 d = 17.0f; d < len - 10.0f; d += 32.0f) {
      const vec2 mark = path[i - 1] + dir * d;
      draw_line(ctx, mark, mark + dir * 13.0f, 2.0f, rgba{0.38f, 0.33f, 0.26f, 0.38f});
    }
  }
  for (const vec2 point : path)
    draw_circle(ctx, point, road_width * 0.5f - 1.0f, road_light);

  // Gate stones at the breach and the crystal protected by the keep.
  draw_rect(ctx, {{field_x - 2.0f, path.front().y - 25.0f}, {9.0f, 50.0f}}, rgb(219, 226, 207));
  draw_rect(ctx, {{field_x + field_w - 7.0f, path.back().y - 26.0f}, {12.0f, 52.0f}}, rgb(227, 222, 198));
  draw_circle(ctx, path.back() + vec2{-16.0f, 0.0f}, 18.0f, rgb(27, 46, 49));
  draw_circle(ctx, path.back() + vec2{-16.0f, 0.0f}, 12.0f, rgb(116, 224, 216));
  draw_triangle(ctx, path.back() + vec2{-16.0f, -9.0f}, path.back() + vec2{-8.0f, 0.0f},
                path.back() + vec2{-16.0f, 9.0f}, rgb(216, 255, 237));
  text(ctx, "RANH GIỚI", field_x + 11.0f, path.front().y - 44.0f, 10.0f, paper);
  text(ctx, "THÀNH TRÌ", field_x + field_w - 90.0f, path.back().y - 47.0f, 10.0f, paper);
  draw_rect_lines(ctx, field, 2.0f, rgb(131, 164, 129));
}

void draw_tower(njin_ctx &ctx, vec2 pos, tower_kind kind, i32 level, f32 rotation, bool selected, f32 scale = 1.0f) {
  const tower_spec &spec = specs[static_cast<usize>(kind)];
  draw_circle(ctx, pos + vec2{1.0f, 5.0f} * scale, 18.5f * scale, rgba{0.04f, 0.1f, 0.1f, 0.32f});
  draw_circle(ctx, pos, 18.0f * scale, rgb(33, 47, 53));
  draw_circle(ctx, pos, 14.0f * scale, rgb(111, 132, 127));
  draw_circle(ctx, pos, 11.5f * scale, rgb(45, 61, 67));

  if (kind == tower_kind::archer) {
    draw_circle(ctx, pos, 8.0f * scale, spec.color);
    draw_triangle(ctx, pos + vec2{-5.0f, 1.0f} * scale, pos + vec2{0.0f, -10.0f} * scale,
                  pos + vec2{5.0f, 1.0f} * scale, rgb(206, 255, 228));
    draw_line(ctx, pos + vec2{-7.0f, 6.0f} * scale, pos + vec2{7.0f, 6.0f} * scale, 2.0f * scale, rgb(36, 78, 71));
  } else if (kind == tower_kind::mage) {
    draw_circle(ctx, pos, 8.0f * scale, rgb(65, 53, 103));
    draw_triangle(ctx, pos + vec2{0.0f, -12.0f} * scale, pos + vec2{10.0f, 1.0f} * scale,
                  pos + vec2{0.0f, 11.0f} * scale, spec.color);
    draw_triangle(ctx, pos + vec2{0.0f, -12.0f} * scale, pos + vec2{-10.0f, 1.0f} * scale,
                  pos + vec2{0.0f, 11.0f} * scale, rgb(221, 208, 255));
  } else {
    draw_circle(ctx, pos, 9.0f * scale, rgb(85, 61, 47));
    draw_rect_rotated(ctx, pos + from_angle(rotation) * (8.0f * scale), {20.0f * scale, 8.0f * scale}, rotation,
                      rgb(49, 60, 63));
    draw_circle(ctx, pos, 7.0f * scale, spec.color);
    draw_circle(ctx, pos, 3.0f * scale, rgb(255, 226, 169));
  }
  for (i32 i = 0; i < std::min(level, 3); ++i)
    draw_circle(ctx, pos + vec2{-5.0f + i * 5.0f, 14.0f} * scale, 1.5f * scale, gold);
  if (selected) {
    draw_circle_lines(ctx, pos, 21.0f * scale, 2.0f, rgb(255, 244, 199));
    draw_circle_lines(ctx, pos, spec.range + (level - 1) * 8.0f, 1.0f, rgba{spec.color.r, spec.color.g, spec.color.b, 0.18f});
  }
}

void draw_field_actors(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  for (auto [e, tr, tw] : reg.view<const transform, const tower_component>().each()) {
    const bool selected = game.selected == e;
    f32 facing = 0.0f;
    const tower_spec &spec = specs[static_cast<usize>(tw.kind)];
    if (tw.kind == tower_kind::cannon) {
      f32 nearest = spec.range;
      for (auto [foe_e, foe_tr, foe] : reg.view<const transform, const enemy_component>().each()) {
        (void)foe_e;
        const f32 d = distance(tr.pos, foe_tr.pos);
        if (d < nearest) {
          nearest = d;
          facing = angle_of(foe_tr.pos - tr.pos);
        }
      }
    }
    draw_tower(ctx, tr.pos, tw.kind, tw.level, facing, selected, tr.scale);
  }
  for (auto [e, tr, shot] : reg.view<const transform, const projectile_component>().each()) {
    (void)e;
    draw_line(ctx, tr.pos, shot.last_target, 1.0f, rgba{shot.color.r, shot.color.g, shot.color.b, 0.18f});
    draw_circle(ctx, tr.pos, 6.0f, rgba{shot.color.r, shot.color.g, shot.color.b, 0.2f});
    draw_circle(ctx, tr.pos, 3.0f, shot.color);
  }
  for (auto [e, tr, foe] : reg.view<const transform, const enemy_component>().each()) {
    (void)e;
    if (foe.hp <= 0.0f)
      continue;
    const f32 bar_y = tr.pos.y - foe.radius * tr.scale - 8.0f;
    draw_rect(ctx, {{tr.pos.x - 14.0f, bar_y}, {28.0f, 4.0f}}, rgb(34, 43, 44));
    draw_rect(ctx, {{tr.pos.x - 13.0f, bar_y + 1.0f}, {26.0f * clamp(foe.hp / foe.max_hp, 0.0f, 1.0f), 2.0f}},
              foe.hp / foe.max_hp < 0.33f ? red : green);
  }
}

void draw_background(njin_ctx &ctx) { draw_road(ctx); }

void draw_field(njin_ctx &ctx) {
  if (game.screen == game_screen::playing) {
    const vec2 mouse = mouse_pos(ctx);
    if (game.build_kind >= 0 && inside(field, mouse)) {
      const vec2 ghost = snapped(mouse);
      const tower_spec &spec = specs[static_cast<usize>(game.build_kind)];
      const bool valid = placement_ok(ctx, ghost) && game.gold >= spec.cost;
      draw_circle(ctx, ghost, spec.range, rgba{spec.color.r, spec.color.g, spec.color.b, 0.055f});
      draw_circle_lines(ctx, ghost, spec.range, 1.0f, rgba{spec.color.r, spec.color.g, spec.color.b, 0.4f});
      draw_circle(ctx, ghost, 18.0f, rgba{spec.color.r, spec.color.g, spec.color.b, valid ? 0.58f : 0.25f});
      draw_circle_lines(ctx, ghost, 18.0f, 2.0f, valid ? paper : red);
    }
    draw_field_actors(ctx);
  } else {
    // Decorative sentries make the title screen read as a game, while all gameplay entities stay in ECS.
    draw_tower(ctx, {field_x + 195.0f, field_y + 38.0f}, tower_kind::archer, 1, 0.0f, false);
    draw_tower(ctx, {field_x + 533.0f, field_y + 478.0f}, tower_kind::mage, 1, 0.0f, false);
    draw_tower(ctx, {field_x + 799.0f, field_y + 142.0f}, tower_kind::cannon, 1, 0.0f, false);
  }
}

void draw_sidebar(njin_ctx &ctx) {
  if (game.screen != game_screen::playing)
    return;
  ui_begin(ctx, {.id = "armory", .title = "Kho vũ khí", .anchor = {1.0f, 0.0f}, .pivot = {1.0f, 0.0f}, .width = 340.0f});

  char stats[100];
  std::snprintf(stats, sizeof stats, "Vàng: %d    Thành trì: %d/20    Đợt: %d/%d", game.gold, game.lives, game.wave, wave_limit);
  ui_label(ctx, stats);
  ui_label(ctx, "Chọn tháp rồi bấm lên bãi cỏ trống.");
  if (ui_button(ctx, "Cung thủ - 75 vàng", game.gold >= specs[0].cost))
    game.build_kind = game.build_kind == 0 ? -1 : 0;
  if (ui_button(ctx, "Pháp sư - 120 vàng", game.gold >= specs[1].cost))
    game.build_kind = game.build_kind == 1 ? -1 : 1;
  if (ui_button(ctx, "Pháo thủ - 145 vàng", game.gold >= specs[2].cost))
    game.build_kind = game.build_kind == 2 ? -1 : 2;

  if (game.build_kind >= 0) {
    char selected_build[64];
    std::snprintf(selected_build, sizeof selected_build, "Đang đặt: %s", specs[static_cast<usize>(game.build_kind)].name);
    ui_label(ctx, selected_build);
  }

  if (game.selected != entt::null && world(ctx).valid(game.selected) && world(ctx).all_of<tower_component>(game.selected)) {
    const tower_component &tw = world(ctx).get<tower_component>(game.selected);
    const tower_spec &spec = specs[static_cast<usize>(tw.kind)];
    char details[120];
    std::snprintf(details, sizeof details, "%s - cấp %d - sát thương %d - tầm %d", spec.name, tw.level,
                  static_cast<i32>(spec.damage * (1.0f + 0.34f * (tw.level - 1))),
                  static_cast<i32>(spec.range + (tw.level - 1) * 8.0f));
    ui_label(ctx, details);
    const i32 upgrade_cost = 38 + tw.level * 32;
    char cost[40];
    std::snprintf(cost, sizeof cost, "Nâng cấp (%d vàng)", upgrade_cost);
    ui_row(ctx, 2);
    if (ui_button(ctx, cost, game.gold >= upgrade_cost))
      upgrade_selected(ctx);
    if (ui_button(ctx, "Thu hồi"))
      sell_selected(ctx);
  } else {
    ui_label(ctx, "Cung thủ bắn nhanh. Pháp sư và pháo gây sát thương diện rộng.");
  }

  ui_row(ctx, 2);
  if (ui_button(ctx, game.paused ? "Tiếp tục" : "Tạm dừng"))
    game.paused = !game.paused;
  char speed[24];
  std::snprintf(speed, sizeof speed, "Tốc độ x%d", game.speed);
  if (ui_button(ctx, speed))
    game.speed = game.speed == 1 ? 2 : 1;

  const char *wave_label = game.wave_active ? "Đợt đang tiến công" : "Bắt đầu đợt";
  if (ui_button(ctx, wave_label, !game.wave_active && !game.paused && game.wave < wave_limit))
    launch_wave(ctx);
  ui_label(ctx, "Esc hủy chọn tháp đang đặt.");
  if (ui_back(ctx)) {
    game.build_kind = -1;
    game.selected = entt::null;
  }
  ui_end(ctx);
}

void draw_overlay(njin_ctx &ctx) {
  if (game.screen == game_screen::intro) {
    ui_begin(ctx, {.id = "title", .title = "Thành Trì Bình Minh", .anchor = {0.5f, 0.5f},
                   .pivot = {0.5f, 0.5f}, .width = 460.0f});
    ui_label(ctx, "Dựng tháp, chặn quân địch, bảo vệ tinh thạch.");
    ui_label(ctx, "8 đợt tấn công, 3 loại tháp, có thể nâng cấp và thu hồi.");
    if (ui_button(ctx, "Vào phòng thủ"))
      reset_game(ctx);
    ui_end(ctx);
  } else if (game.screen == game_screen::ended && game.end_popup_open) {
    const i32 picked = ui_popup(ctx,
        {.id = "result",
         .title = game.lives > 0 ? "Đã giữ vững thành trì!" : "Thành trì thất thủ",
         .message = game.lives > 0 ? "Bình minh đã trở lại. Bạn đã đẩy lùi cả tám đợt quân địch."
                                  : "Quân địch đã tràn qua phòng tuyến. Hãy thử cách bố trí khác.",
         .buttons = {"Chơi lại", "Về tiêu đề"},
         .default_button = 1,
         .cancel_button = 1,
         .width = 460.0f},
        game.end_popup_open);
    if (picked == 0) {
      reset_game(ctx);
    } else if (picked == 1) {
      return_to_title(ctx);
    }
  }
}

} // namespace defense

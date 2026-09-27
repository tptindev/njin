#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace paper_crowd {
game_state g;

namespace {
void startup(njin_ctx &ctx) {
  g.paper = shader_load(ctx, "assets/paper.vs", "assets/paper.fs");
  g.person = shader_load(ctx, "assets/person.vs", "assets/person.fs");
  // 12 floats a person: see `instance` in draw.cpp.
  g.instances = instance_buffer_create(ctx, 12);
  for (usize i = 0; i < cloth_colors.size(); ++i) {
    char name[24];
    std::snprintf(name, sizeof name, "cloth[%d]", static_cast<i32>(i));
    const rgba c = cloth_colors[i];
    shader_set_vec4(ctx, g.person, name, {c.r, c.g, c.b, 1.0f});
  }

  debug_component<person>(ctx, "person", [](const person &p) {
    return json_value::make_object()
        .set("act", static_cast<i32>(p.act))
        .set("timer", p.timer)
        .set("ring", p.ring);
  });
  debug_component<pet>(ctx, "pet", [](const pet &p) {
    return json_value::make_object().set("cat", p.cat).set("sniffing", p.sniffing);
  });

  spawn_camera(ctx);
  spawn_crowd(ctx);
}

void handle_input(njin_ctx &ctx) {
  g.call_flash = std::max(0.0f, g.call_flash - delta(ctx));
  const vec2 m = scr2w(ctx, mouse_pos(ctx));
  const bool on_paper = m.x > 0.0f && m.y > 0.0f && m.x < world_w && m.y < world_h;

  // Clicking someone follows them; clicking empty paper adds a person.
  if (on_paper && mouse_pressed(ctx, mouse_left)) {
    const entt::entity hit = person_at(ctx, m, 11.0f);
    if (hit != entt::null)
      focus_on(hit);
    else if (static_cast<i32>(world(ctx).view<person>().size()) < max_people)
      spawn_person(ctx, m);
  }
  // The wheel zooms in on whoever is nearest the cursor, zooms further while
  // following, and scrolling back out past focus_zoom_min lets go.
  if (const f32 wheel = mouse_wheel(ctx); wheel != 0.0f) {
    if (g.focus == entt::null) {
      const entt::entity near = wheel > 0.0f ? person_at(ctx, m, 80.0f) : entt::null;
      if (near != entt::null) {
        g.focus_zoom = 2.5f;
        focus_on(near);
      }
    } else {
      g.focus_zoom = std::min(g.focus_zoom * std::pow(1.25f, wheel), focus_zoom_max);
      if (g.focus_zoom < focus_zoom_min)
        unfocus();
    }
  }
  // Esc lets go of the focus; with nobody followed it closes the game.
  if (key_pressed(ctx, key_escape)) {
    if (g.focus != entt::null)
      unfocus();
    else
      njin_quit(ctx);
  }
  if (on_paper && mouse_pressed(ctx, mouse_right)) {
    call_people(ctx, m, 190.0f);
    g.call_pos = m;
    g.call_flash = 0.8f;
  }
  if (key_pressed(ctx, key_space))
    cheer_all(ctx);
  if (key_pressed(ctx, key_r)) {
    unfocus();
    clear_crowd(ctx);
    spawn_crowd(ctx);
  }
  if (key_pressed(ctx, key_h))
    g.show_hint = !g.show_hint;
}

void setup(njin_ctx &ctx) {
  ecs_register(ctx, phase_startup, startup, "startup");
  ecs_register(ctx, phase_update, handle_input, "input");
  ecs_register(ctx, phase_update, drive_rings, "drive_rings");
  ecs_register(ctx, phase_update, drive_people, "drive_people");
  ecs_register(ctx, phase_update, drive_pets, "drive_pets");
  ecs_register(ctx, phase_post_update, drive_camera, "camera");
  ecs_register(ctx, phase_pre_render, draw_paper, "paper");
  ecs_register(ctx, phase_render, draw_crowd, "crowd");
  ecs_register(ctx, phase_post_render, draw_hint, "hint");
}
} // namespace

mod_desc module() { return {.name = "paper_crowd", .setup = setup}; }
} // namespace paper_crowd

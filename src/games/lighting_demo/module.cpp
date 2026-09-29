#include "demo.h"

#include <algorithm>
#include <vector>

namespace lighting_demo {
namespace {
// Leaves the current room (every entity it made is destroyed) and builds room `index`.
void enter_room(context &ctx, i32 index) {
  entt::registry &reg = world(ctx);
  const auto view = reg.view<room_entity>();
  const std::vector<entt::entity> old(view.begin(), view.end());
  reg.destroy(old.begin(), old.end());
  demo.labels.clear();
  demo.current = (index % room_count + room_count) % room_count;
  room_at(demo.current).build(ctx);
}

void startup(context &ctx) {
  load_images(ctx);
  draw_set_y_sort(ctx, layer_things, true); // things standing on the ground overlap by their feet
  demo.camera = camera_spawn(ctx, camera_zoom, room_centre);
  enter_room(ctx, 0);
}

void input(context &ctx) {
  // Going from room to room. On a laptop Page Up / Down and F1..F12 often need Fn, so Tab, the number
  // keys and the two buttons of the bottom bar do it too.
  const bool shift = key_held(ctx, key_left_shift) || key_held(ctx, key_right_shift);
  if (key_pressed(ctx, key_page_down) || (key_pressed(ctx, key_tab) && !shift))
    enter_room(ctx, demo.current + 1);
  if (key_pressed(ctx, key_page_up) || (key_pressed(ctx, key_tab) && shift))
    enter_room(ctx, demo.current - 1);
  for (i32 i = 0; i < room_count; i++)
    if (key_pressed(ctx, (key_code)((i32)key_f1 + i)))
      enter_room(ctx, i);
  for (i32 i = 0; i < 10; i++) // 1..9 are rooms 1 to 9, 0 is room 10
    if (key_pressed(ctx, (key_code)((i32)key_0 + i)))
      enter_room(ctx, i == 0 ? 9 : i - 1);
  if (mouse_pressed(ctx, mouse_left)) {
    rect prev, next;
    nav_buttons(ctx, prev, next);
    const vec2 m = mouse_pos(ctx);
    const auto hit = [&m](rect r) { return m.x >= r.pos.x && m.x < r.pos.x + r.size.x && m.y >= r.pos.y && m.y < r.pos.y + r.size.y; };
    if (hit(prev) || hit(next)) {
      enter_room(ctx, demo.current + (hit(next) ? 1 : -1));
      mouse_consume(ctx, mouse_left); // not a click for the room (room 7 digs walls)
    }
  }
  if (key_pressed(ctx, key_l)) {
    // The whole comparison: the same scene without lighting.
    demo.lit = !demo.lit;
    lighting_desc d = lighting_get(ctx);
    d.enabled = demo.lit;
    lighting_set(ctx, d);
  }
  if (key_pressed(ctx, key_g))
    demo.outlines = !demo.outlines;
  if (key_pressed(ctx, key_h))
    demo.help = !demo.help;
  const room &r = room_at(demo.current);
  if (r.update != nullptr)
    r.update(ctx);
}
} // namespace

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, startup, "startup");
  ecs_register(ctx, phase_update, input, "input");
  ecs_register(ctx, phase_render, draw_world, "draw_world");
  ecs_register(ctx, phase_post_render, hud, "hud");
}
} // namespace lighting_demo

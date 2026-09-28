#include "demo.h"

#include <algorithm>
#include <vector>

namespace lighting_demo {
namespace {
// Leaves the current room (every entity it made is destroyed) and builds room `index`.
void enter_room(njin_ctx &ctx, i32 index) {
  entt::registry &reg = world(ctx);
  const auto view = reg.view<room_entity>();
  const std::vector<entt::entity> old(view.begin(), view.end());
  reg.destroy(old.begin(), old.end());
  demo.labels.clear();
  demo.current = (index % room_count + room_count) % room_count;
  room_at(demo.current).build(ctx);
}

void startup(njin_ctx &ctx) {
  load_images(ctx);
  draw_set_y_sort(ctx, layer_things, true); // things standing on the ground overlap by their feet
  demo.camera = camera_spawn(ctx, camera_zoom, room_centre);
  enter_room(ctx, 0);
}

void input(njin_ctx &ctx) {
  if (key_pressed(ctx, key_page_down))
    enter_room(ctx, demo.current + 1);
  if (key_pressed(ctx, key_page_up))
    enter_room(ctx, demo.current - 1);
  for (i32 i = 0; i < room_count; i++)
    if (key_pressed(ctx, (key_code)((i32)key_f1 + i)))
      enter_room(ctx, i);
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

void setup(njin_ctx &ctx) {
  ecs_register(ctx, phase_startup, startup, "startup");
  ecs_register(ctx, phase_update, input, "input");
  ecs_register(ctx, phase_render, draw_world, "draw_world");
  ecs_register(ctx, phase_post_render, hud, "hud");
}
} // namespace lighting_demo

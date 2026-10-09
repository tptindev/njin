#include <njin.h>

namespace {
using namespace njin;

const camera3d camera{.position = {5.5f, 3.5f, 6.5f}, .target = {-0.5f, 0.8f, -0.5f}, .fovy = 50.0f};

// The room's walls, to find where the mouse points: centre and size.
struct box {
  vec3 center, size;
};
const box walls[] = {{{-3.0f, 1.5f, 0.0f}, {0.3f, 3.0f, 8.0f}}, {{0.0f, 1.5f, -3.0f}, {8.0f, 3.0f, 0.3f}}};

post3d fx{};

void load(context &ctx) {
  // Turn each effect on; each is off at 0.
  fx.ssao = 0.8f;       // wall corners and the feet of objects darken
  fx.ssr = 1.0f;        // the polished floor (material3d::reflect below) reflects
  fx.motion_blur = 0.5f; // smears when the camera turns
  post3d_set(ctx, fx);
}

void update(context &ctx) {
  // Keys 1, 2, 3, 4 toggle SSAO, reflections, motion blur, TAA.
  if (key_pressed(ctx, key_1))
    fx.ssao = fx.ssao > 0.0f ? 0.0f : 0.8f;
  if (key_pressed(ctx, key_2))
    fx.ssr = fx.ssr > 0.0f ? 0.0f : 1.0f;
  if (key_pressed(ctx, key_3))
    fx.motion_blur = fx.motion_blur > 0.0f ? 0.0f : 0.5f;
  if (key_pressed(ctx, key_4))
    fx.taa = !fx.taa;
  post3d_set(ctx, fx);

  // Left mouse button: a bullet hole where the mouse points (floor or wall), fading after 10 seconds.
  if (mouse_pressed(ctx, mouse_left)) {
    const ray3d ray = camera3d_ray(ctx, camera, mouse_pos(ctx));
    ray3d_hit hit = ray3d_plane(ray, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    for (const box &w : walls) {
      const ray3d_hit h = ray3d_box(ray, w.center, w.size);
      if (h.hit && (!hit.hit || h.distance < hit.distance))
        hit = h;
    }
    if (hit.hit)
      decal3d_add(ctx, {.position = hit.point,
                        .rotation = decal3d_rotation(hit.normal),
                        .size = {0.3f, 0.2f, 0.3f},
                        .color = {0.05f, 0.05f, 0.05f, 0.9f},
                        .lifetime = 10.0f,
                        .fade = 2.0f});
  }
}

void render(context &ctx) {
  light3d_set(ctx, {.direction = {-0.45f, -1.0f, -0.3f}, .shadows = true, .shadow_range = 15.0f});
  begin_3d(ctx, camera);
  // A polished floor: reflects when post3d::ssr is on.
  material3d floor{};
  floor.reflect = 0.6f;
  material3d_set(ctx, floor);
  draw_plane3d(ctx, {0.0f, 0.0f, 0.0f}, {20.0f, 20.0f}, {0.7f, 0.7f, 0.72f, 1.0f});
  material3d_set(ctx, {});
  for (const box &w : walls)
    draw_cube3d(ctx, w.center, w.size, {0.85f, 0.8f, 0.7f, 1.0f});
  draw_sphere3d(ctx, {1.0f, 1.0f, 0.0f}, 1.0f, {0.85f, 0.2f, 0.15f, 1.0f});
  end_3d(ctx);
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, load, "load");
  ecs_register(ctx, phase_update, update, "update");
  ecs_register(ctx, phase_render, render, "render");
}
} // namespace

mod_desc screen_fx_module() { return {.name = "screen_fx", .setup = setup}; }

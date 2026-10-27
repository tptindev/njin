// njin_skylight: sunbeams through a skylight in a dark hall, onto a statue.
//
// The sun comes in through a skylight of 3 x 3 openings and is seen as nine
// shafts in thin volumetric fog (light3d::volume_*), lands on the statue and
// the floor in nine bright patches (with bloom), and lights the hall a little
// by bouncing (irradiance3d). With njin built with NJIN_RAYTRACING and a GPU
// that has ray tracing, the shadows are ray traced (soft with distance) and
// the bounce is traced every frame, so it follows what is moved; without,
// shadow maps and a captured grid stand in.
//
// Mouse: right drag turns round, middle drag (or Shift + right) pans, the wheel
// zooms. Left drag moves the statue, the block or the ball over the floor;
// with Shift, up and down. Keys 1 to 4 turn the ray traced shadows, the
// bounce, the fog and the bloom on and off; R puts the camera back.
#include <njin.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace njin;

namespace {
// The hall: 20 x 10 x 20 m, the skylight round (5, 10, -1.25), from where the
// sun reaches the top of the statue.
constexpr f32 hall = 10.0f, height = 10.0f;
constexpr vec3 sun_dir{-0.6f, -0.78f, 0.15f};
constexpr rgba wall{0.3f, 0.28f, 0.25f, 1.0f};

struct thing {
  const char *name;
  vec3 pos; // on the floor: the statue's base, the block's and the ball's bottom
  vec3 size;
};

struct game {
  model_handle statue{};
  f32 statue_scale = 1.0f;
  thing things[3] = {{"tượng", {0.0f, 0.0f, 0.0f}, {}},
                     {"khối đá", {-3.2f, 0.0f, 1.8f}, {1.2f, 1.2f, 1.2f}},
                     {"quả cầu", {2.6f, 0.0f, 2.8f}, {1.0f, 1.0f, 1.0f}}};
  camera3d_orbit orbit;
  // The thing being dragged (-1 none), where it was grabbed and from how far off.
  i32 held = -1;
  vec3 grab{};
  vec3 offset{};
  bool rt_shadows = true, bounce = true, fog = true, bloom = true;
  bool rt = false; // raytracing_available()
};
game g;

void reset_camera() {
  // From about (0, 4.5, 8), looking at the statue's upper half and the shafts.
  g.orbit = {.target = {1.5f, 4.0f, -0.5f}, .yaw = -10.0f, .pitch = 3.3f, .distance = 8.6f, .pitch_min = -80.0f,
             .pitch_max = 80.0f, .distance_min = 1.5f, .distance_max = 30.0f, .fovy = 65.0f};
}

camera3d camera() { return camera3d_orbit_view(g.orbit); }

// The bounce: traced every frame where it can be, else captured once.
void set_bounce(context &ctx) {
  if (!g.bounce) {
    irradiance3d_clear(ctx);
    return;
  }
  irradiance3d_set(ctx, {.center = {0.0f, height * 0.5f, 0.0f},
                         .size = {2.0f * hall - 0.4f, height - 0.4f, 2.0f * hall - 0.4f},
                         .count_x = 10, .count_y = 6, .count_z = 10, .blend = 0.05f, .ray_traced = true, .rays = 128});
}

void startup(context &ctx) {
  g.statue = model_load(ctx, model_load_desc{.path = "assets/snake_statue.glb", .pbr = true});
  vec3 lo{}, hi{};
  if (model_bounds(ctx, g.statue, &lo, &hi))
    g.statue_scale = 4.0f / std::max(hi.y - lo.y, 0.01f); // 4 m tall
  g.things[0].size = (hi - lo) * g.statue_scale;
  g.rt = raytracing_available(ctx);
  g.rt_shadows = g.rt;
  reset_camera();
  set_bounce(ctx);
}

// Where the ray under the mouse meets thing `i`.
ray3d_hit hit_thing(context &ctx, const ray3d &ray, i32 i) {
  const thing &t = g.things[i];
  if (i == 0)
    return ray3d_model(ctx, ray, g.statue, {.position = t.pos, .scale = vec3{1, 1, 1} * g.statue_scale});
  if (i == 1)
    return ray3d_box(ray, t.pos + vec3{0.0f, t.size.y * 0.5f, 0.0f}, t.size);
  return ray3d_sphere(ray, t.pos + vec3{0.0f, t.size.y * 0.5f, 0.0f}, t.size.y * 0.5f);
}

void update(context &ctx) {
  const bool shift = key_held(ctx, key_left_shift);
  // Turn round (right drag), pan (middle, or Shift + right), zoom (wheel).
  camera3d_orbit_update(ctx, g.orbit);

  // Pick and drag.
  const camera3d cam = camera();
  const ray3d ray = camera3d_ray(ctx, cam, mouse_pos(ctx));
  if (mouse_pressed(ctx, mouse_left)) {
    g.held = -1;
    f32 nearest = 1e30f;
    for (i32 i = 0; i < 3; i++) {
      const ray3d_hit h = hit_thing(ctx, ray, i);
      if (h.hit && h.distance < nearest) {
        nearest = h.distance;
        g.held = i;
        g.grab = h.point;
        g.offset = g.things[i].pos - h.point;
      }
    }
  }
  if (mouse_released(ctx, mouse_left))
    g.held = -1;
  if (g.held >= 0 && mouse_held(ctx, mouse_left)) {
    thing &t = g.things[g.held];
    if (shift) {
      // Up and down: on the upright plane through the grab point facing the camera.
      vec3 f = cam.target - cam.position;
      f.y = 0.0f;
      const ray3d_hit h = ray3d_plane(ray, g.grab, normalize(f) * -1.0f);
      if (h.hit)
        t.pos.y = std::clamp(h.point.y + g.offset.y, 0.0f, height - t.size.y);
    } else {
      // Over the floor: on the level plane at the height it was grabbed.
      const ray3d_hit h = ray3d_plane(ray, g.grab, {0.0f, 1.0f, 0.0f});
      if (h.hit) {
        const f32 rx = hall - t.size.x * 0.5f, rz = hall - t.size.z * 0.5f;
        t.pos.x = std::clamp(h.point.x + g.offset.x, -rx, rx);
        t.pos.z = std::clamp(h.point.z + g.offset.z, -rz, rz);
      }
    }
  }

  if (key_pressed(ctx, key_r))
    reset_camera();
  if (key_pressed(ctx, key_1))
    g.rt_shadows = !g.rt_shadows;
  if (key_pressed(ctx, key_2)) {
    g.bounce = !g.bounce;
    set_bounce(ctx);
  }
  if (key_pressed(ctx, key_3))
    g.fog = !g.fog;
  if (key_pressed(ctx, key_4))
    g.bloom = !g.bloom;
}

void draw_hall(context &ctx) {
  // Floor and walls; the ceiling round the skylight. Every piece reaches into
  // the next: boxes that only touch at an edge let light through.
  const f32 s = 2.0f * hall + 0.4f;
  draw_cube3d(ctx, {0.0f, -0.1f, 0.0f}, {s, 0.2f, s}, wall);
  draw_cube3d(ctx, {-hall - 0.1f, height * 0.5f, 0.0f}, {0.2f, height, s}, wall);
  draw_cube3d(ctx, {hall + 0.1f, height * 0.5f, 0.0f}, {0.2f, height, s}, wall);
  draw_cube3d(ctx, {0.0f, height * 0.5f, -hall - 0.1f}, {s, height, 0.2f}, wall);
  draw_cube3d(ctx, {0.0f, height * 0.5f, hall + 0.1f}, {s, height, 0.2f}, wall);
  // 3 x 3 openings of 0.8 m, 0.45 m beams between them: 3.3 m a side.
  const f32 half = 1.65f;
  const f32 x0 = 5.0f - half, x1 = 5.0f + half;
  const f32 z0 = -1.25f - half, z1 = -1.25f + half;
  const f32 y = height + 0.1f, e = hall + 0.2f;
  draw_cube3d(ctx, {(-e + x0) * 0.5f, y, 0.0f}, {x0 + e, 0.2f, s}, wall);
  draw_cube3d(ctx, {(x1 + e) * 0.5f, y, 0.0f}, {e - x1, 0.2f, s}, wall);
  draw_cube3d(ctx, {(x0 + x1) * 0.5f, y, (-e + z0) * 0.5f}, {x1 - x0 + 0.02f, 0.2f, z0 + e}, wall);
  draw_cube3d(ctx, {(x0 + x1) * 0.5f, y, (z1 + e) * 0.5f}, {x1 - x0 + 0.02f, 0.2f, e - z1}, wall);
  for (i32 i = 0; i < 2; i++) {
    const f32 at = 0.8f + 0.225f + (f32)i * 1.25f; // the beams' middles from the edge
    draw_cube3d(ctx, {x0 + at, y, (z0 + z1) * 0.5f}, {0.45f, 0.2f, z1 - z0 + 0.02f}, wall);
    draw_cube3d(ctx, {(x0 + x1) * 0.5f, y, z0 + at}, {x1 - x0 + 0.02f, 0.2f, 0.45f}, wall);
  }
}

void render(context &ctx) {
  light3d l{};
  l.direction = sun_dir;
  l.color = {8.0f, 7.5f, 6.6f, 1.0f};
  l.ambient = {0.002f, 0.002f, 0.002f, 1.0f};
  l.shadows = true;
  l.shadow_range = 22.0f; // the whole hall: the shadows of what lies far off
  l.shadow_size = 2048;
  l.shadow_softness = 0.5f;
  l.tonemap = tonemap3d_agx;
  l.hdr = true;
  l.ray_traced_shadows = g.rt_shadows;
  l.sun_angle = 0.8f;
  if (g.fog) {
    l.volume_density = 0.000425f;
    l.volume_ambient = 0.0f;
    l.volume_anisotropy = 0.5f;
    l.volume_distance = 40.0f;
    // The hall is deep: a step every 25 cm at most, so the narrow shafts are
    // not stepped over; and the fog outside the sun's map stays dark (indoors).
    l.volume_step_max = 0.25f;
    l.volume_dark_outside = true;
  }
  light3d_set(ctx, l);
  post3d_set(ctx, {.bloom = g.bloom ? 0.08f : 0.0f, .bloom_radius = 1.2f});

  begin_3d(ctx, camera());
  draw_hall(ctx);
  const thing &st = g.things[0];
  draw_model(ctx, g.statue, {.position = st.pos, .scale = vec3{1, 1, 1} * g.statue_scale});
  const rgba stone{0.62f, 0.58f, 0.52f, 1.0f};
  material3d_set(ctx, {.pbr = true, .roughness = 0.8f});
  draw_cube3d(ctx, g.things[1].pos + vec3{0.0f, g.things[1].size.y * 0.5f, 0.0f}, g.things[1].size, stone);
  material3d_set(ctx, {.pbr = true, .metallic = 0.9f, .roughness = 0.25f});
  draw_sphere3d(ctx, g.things[2].pos + vec3{0.0f, g.things[2].size.y * 0.5f, 0.0f}, g.things[2].size.y * 0.5f,
                {0.9f, 0.75f, 0.45f, 1.0f});
  material3d_set(ctx, {});
  end_3d(ctx);
}

// A line of text on a dark band, readable over the bright patches of sun.
void label(context &ctx, const char *text, vec2 at, f32 size, rgba color) {
  const vec2 m = text_measure(ctx, text, size);
  draw_rect(ctx, {at - vec2{8.0f, 5.0f}, m + vec2{16.0f, 10.0f}}, {0.0f, 0.0f, 0.0f, 0.6f});
  draw_text(ctx, text, at, size, color);
}

void render_ui(context &ctx) {
  const rgba dim{0.78f, 0.78f, 0.75f, 1.0f};
  char line[200];
  std::snprintf(line, sizeof line, "1 bóng bằng tia: %s   2 ánh sáng dội: %s   3 sương: %s   4 bloom: %s",
                !g.rt ? "không có" : g.rt_shadows ? "bật" : "tắt", g.bounce ? (g.rt ? "bằng tia" : "chụp") : "tắt",
                g.fog ? "bật" : "tắt", g.bloom ? "bật" : "tắt");
  label(ctx, line, {16.0f, 14.0f}, 18.0f, dim);
  if (g.held >= 0) {
    std::snprintf(line, sizeof line, "Đang kéo: %s", g.things[g.held].name);
    label(ctx, line, {16.0f, 44.0f}, 18.0f, colors::white);
  }
  const vec2 screen = screen_size(ctx);
  label(ctx, "Chuột phải: xoay   Chuột giữa/Shift+phải: dời   Lăn: zoom   Chuột trái: kéo vật (Shift: nâng)   R: camera",
        {16.0f, screen.y - 30.0f}, 16.0f, dim);
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, startup, "skylight_startup");
  ecs_register(ctx, phase_update, update, "skylight_update");
  ecs_register(ctx, phase_render, render, "skylight_render");
  ecs_register(ctx, phase_post_render, render_ui, "skylight_ui");
}
} // namespace

int main() {
  const config cfg{.title = "Giếng trời",
                   .width = 1280.0f,
                   .height = 720.0f,
                   .target_fps = 60.0f,
                   .clear_bg_color = {0.0f, 0.0f, 0.0f, 1.0f},
                   .resizable = true,
                   .app_name = "njin skylight"};
  context *ctx = create(cfg);
  mod_register(*ctx, {.name = "skylight", .setup = setup});
  run(*ctx);
  destroy(ctx);
  return 0;
}

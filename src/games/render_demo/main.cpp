// A sample for the rendering side of njin: what to switch on, and what it does
// to the frame. Walk around a big map (a 128 x 96 tile forest with animated
// water and a few thousand trees and rocks) and press:
//
//   1  atlas: the crowd's images packed into one texture, or loaded one by one
//   2  particles: on the GPU (instanced, simulated in a vertex shader) or the CPU
//   3  vsync
//   4  blur (a pause-menu look, half size when wide)
//   5  bloom
//   6  CRT
//   F  a fountain of about 12,000 circles, the load to compare CPU and GPU with
//   R  rain
//   Space  an explosion at the mouse
//   + / -  more / fewer trees and rocks (draws in y order, so their images
//          interleave: the worst case for draw calls)
//   Tab  hide this text
//
// The first line of the HUD is the frame time, the second says how many
// sprites were drawn and how many were skipped for being off screen, and how
// many draw calls the frame took (estimated: raylib does not report them).
// Try 1 with 3000 trees on: the draw calls fall from about a thousand to a
// handful, because sprites of one atlas page are one texture. njin_inspector
// (start it beside the game) shows the same numbers, per-system times and GPU
// memory.
#include <cstdio>
#include <njin.h>
#include <vector>

namespace {
using namespace njin;

constexpr vec2 tile{16.0f, 16.0f};
constexpr i32 map_w = 128;
constexpr i32 map_h = 96;
constexpr vec2 world_size{map_w * 16.0f, map_h * 16.0f};
constexpr i32 layer_ground = 0;
constexpr i32 layer_things = 1;
constexpr f32 camera_zoom = 2.0f;
constexpr i32 crowd_step = 500;

// One of the crowd's images.
enum kind : i32 { k_tree, k_bush, k_rock, k_flower, k_hero, k_spark, kind_count };
constexpr const char *image_paths[kind_count] = {
    "assets/sprites/tree.png",   "assets/sprites/bush.png", "assets/sprites/rock.png",
    "assets/sprites/flower.png", "assets/sprites/hero.png", "assets/sprites/spark.png"};
// Where an image stands on the ground: bottom centre for all but the spark.
constexpr vec2 image_origin{0.5f, 1.0f};

struct crowd_member {
  i32 kind = 0;
};

struct demo_state {
  atlas_handle atlas{};
  texture_handle packed[kind_count]{};   // the same images, in the atlas
  texture_handle separate[kind_count]{}; // and loaded one by one
  bool use_atlas = true;
  bool gpu_particles = true;
  bool blur = false, bloom = false, crt = false;
  bool hud = true;
  bool rain_on = false;
  i32 crowd = 3000;
  entt::entity hero = entt::null;
  entt::entity camera = entt::null;
  entt::entity fountain = entt::null;
  entt::entity rain = entt::null;
  f32 frame_ms = 16.0f; // smoothed
} demo;

texture_handle image(i32 which) { return demo.use_atlas ? demo.packed[which] : demo.separate[which]; }

// --- world ---

void build_map(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  rng &r = random(ctx);
  const texture_handle tileset = texture_load(ctx, "assets/tiles.png");
  texture_set_filter(ctx, tileset, filter_nearest);

  tilemap map{};
  map.tileset = tileset;
  map.tile_size = tile;
  map.layer = layer_ground;
  tilemap_animate(map, 2, {2, 3, 4, 5}, 0.25f);
  // A few lakes: ellipses of water tile 2 (the tilemap animates it).
  struct lake {
    f32 x, y, rx, ry;
  };
  std::vector<lake> lakes;
  for (i32 i = 0; i < 9; i++)
    lakes.push_back({r.range(10.0f, map_w - 10.0f), r.range(10.0f, map_h - 10.0f), r.range(4.0f, 10.0f),
                     r.range(3.0f, 7.0f)});
  for (i32 y = 0; y < map_h; y++) {
    for (i32 x = 0; x < map_w; x++) {
      bool water = false;
      for (const lake &l : lakes) {
        const f32 dx = ((f32)x - l.x) / l.rx;
        const f32 dy = ((f32)y - l.y) / l.ry;
        water = water || dx * dx + dy * dy < 1.0f;
      }
      tilemap_set(map, x, y, water ? 2 : (r.range(0, 4) == 0 ? 1 : 0));
    }
  }
  const entt::entity e = reg.create();
  reg.emplace<transform>(e);
  reg.emplace<tilemap>(e, std::move(map));
}

void spawn_crowd_member(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  rng &r = random(ctx);
  const i32 which = r.range(0, k_flower); // tree, bush, rock or flower (the range includes the top)
  const entt::entity e = reg.create();
  reg.emplace<transform>(e, transform{.pos = {r.range(8.0f, world_size.x - 8.0f), r.range(24.0f, world_size.y - 4.0f)}});
  reg.emplace<sprite>(e, sprite{.texture = image(which), .origin = image_origin, .layer = layer_things});
  reg.emplace<crowd_member>(e, crowd_member{which});
}

void set_crowd(njin_ctx &ctx, i32 wanted) {
  entt::registry &reg = world(ctx);
  demo.crowd = std::clamp(wanted, 0, 40000);
  const i32 have = (i32)reg.view<crowd_member>().size();
  for (i32 i = have; i < demo.crowd; i++)
    spawn_crowd_member(ctx);
  if (have > demo.crowd) {
    std::vector<entt::entity> doomed;
    i32 extra = have - demo.crowd;
    for (const entt::entity e : reg.view<crowd_member>()) {
      if (extra-- <= 0)
        break;
      doomed.push_back(e);
    }
    reg.destroy(doomed.begin(), doomed.end());
  }
}

void build_hero(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  demo.hero = reg.create();
  reg.emplace<transform>(demo.hero, transform{.pos = world_size * 0.5f});
  reg.emplace<sprite>(demo.hero, sprite{.texture = image(k_hero), .origin = image_origin, .layer = layer_things});
  reg.emplace<collider>(demo.hero, collider{.size = {8.0f, 4.0f}, .offset = {0.0f, -2.0f}});
  reg.emplace<topdown_body>(demo.hero, topdown_body{.speed = 110.0f});

  demo.camera = camera_spawn(ctx, camera_zoom, world_size * 0.5f);
  reg.emplace<camera_follow>(demo.camera, camera_follow{.target = demo.hero,
                                                        .smoothing = 0.1f,
                                                        .bounds = rect{{0.0f, 0.0f}, world_size},
                                                        .pixel_snap = true});
}

// --- particles ---

particle_emitter glow_emitter() {
  particle_emitter e{};
  e.emitting = false;
  e.texture = demo.separate[k_spark];
  e.blend = blend_additive;
  e.layer = layer_things;
  return e;
}

void explode(njin_ctx &ctx, vec2 at) {
  particle_emitter e = glow_emitter();
  e.max_particles = 600;
  e.life = {0.6f, 1.4f};
  e.speed = {40.0f, 220.0f};
  e.drag = 1.6f;
  e.gravity = {0.0f, 60.0f};
  e.size_start = 16.0f;
  e.size_end = 2.0f;
  e.color_start = {1.0f, 0.75f, 0.3f, 1.0f};
  e.color_end = {0.9f, 0.15f, 0.05f, 0.0f};
  particles_spawn(ctx, e, at, 500);
}

void build_emitters(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);

  particle_emitter fountain = glow_emitter();
  fountain.max_particles = 30000;
  fountain.rate = 6000.0f;
  fountain.life = {1.5f, 2.5f};
  fountain.speed = {60.0f, 220.0f};
  fountain.angle = -90.0f;
  fountain.spread = 110.0f;
  fountain.gravity = {0.0f, 180.0f};
  fountain.drag = 0.3f;
  fountain.spin = {-90.0f, 90.0f};
  fountain.size_start = 8.0f;
  fountain.size_end = 1.0f;
  fountain.color_start = {0.5f, 0.85f, 1.0f, 0.9f};
  fountain.color_end = {0.2f, 0.3f, 1.0f, 0.0f};
  // Plain circles, the default shape and the costliest on the CPU: raylib draws
  // each as a polygon. (A textured quad is much cheaper there.)
  fountain.texture = {};
  demo.fountain = reg.create();
  reg.emplace<transform>(demo.fountain, transform{.pos = world_size * 0.5f});
  reg.emplace<particle_emitter>(demo.fountain, fountain);

  particle_emitter rain = fx::rain();
  rain.emitting = false;
  rain.rate = 900.0f;
  rain.max_particles = 4000;
  rain.area = {screen_size(ctx).x / camera_zoom + 160.0f, 4.0f};
  rain.layer = layer_things + 1;
  demo.rain = reg.create();
  reg.emplace<transform>(demo.rain);
  reg.emplace<particle_emitter>(demo.rain, rain);
}

// --- settings that the keys flip ---

void apply_post(njin_ctx &ctx) {
  post_fx fx = demo.crt ? post::crt() : post_fx{};
  if (demo.bloom) {
    fx.bloom = 1.0f;
    fx.bloom_threshold = 0.55f;
  }
  if (demo.blur)
    fx.blur = 8.0f;
  post_fx_set(ctx, fx);
}

void apply_atlas(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  for (auto [e, spr, member] : reg.view<sprite, const crowd_member>().each())
    spr.texture = image(member.kind);
  reg.get<sprite>(demo.hero).texture = image(k_hero);
  (void)ctx;
}

// --- systems ---

void startup(njin_ctx &ctx) {
  // Pixel art: nearest sampling everywhere, atlas included.
  demo.atlas = atlas_create(ctx, {.size = 512, .padding = 1, .filter = filter_nearest});
  for (i32 i = 0; i < kind_count; i++) {
    demo.packed[i] = atlas_load(ctx, demo.atlas, image_paths[i]);
    demo.separate[i] = texture_load(ctx, image_paths[i]);
    texture_set_filter(ctx, demo.separate[i], filter_nearest);
  }
  draw_set_y_sort(ctx, layer_things, true);
  build_map(ctx);
  build_hero(ctx);
  build_emitters(ctx);
  set_crowd(ctx, demo.crowd);
  particles_set_backend(ctx, particle_backend_auto);
  debug_watch(ctx, "gpu particles available", particles_gpu_available(ctx));
}

void input(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  vec2 move{};
  move.x = (key_held(ctx, key_d) || key_held(ctx, key_right) ? 1.0f : 0.0f) -
           (key_held(ctx, key_a) || key_held(ctx, key_left) ? 1.0f : 0.0f);
  move.y = (key_held(ctx, key_s) || key_held(ctx, key_down) ? 1.0f : 0.0f) -
           (key_held(ctx, key_w) || key_held(ctx, key_up) ? 1.0f : 0.0f);
  reg.get<topdown_body>(demo.hero).input.move = move;

  if (key_pressed(ctx, key_1)) {
    demo.use_atlas = !demo.use_atlas;
    apply_atlas(ctx);
  }
  if (key_pressed(ctx, key_2)) {
    demo.gpu_particles = !demo.gpu_particles;
    particles_set_backend(ctx, demo.gpu_particles ? particle_backend_auto : particle_backend_cpu);
    // An emitter changes where it runs only once it has no particles left, so
    // clear them to see the switch at once (a game would just let them fly out).
    for (auto [e, em] : reg.view<particle_emitter>().each())
      em.particles.clear();
  }
  if (key_pressed(ctx, key_3))
    window_set_vsync(ctx, !window_vsync(ctx));
  if (key_pressed(ctx, key_4)) {
    demo.blur = !demo.blur;
    apply_post(ctx);
  }
  if (key_pressed(ctx, key_5)) {
    demo.bloom = !demo.bloom;
    apply_post(ctx);
  }
  if (key_pressed(ctx, key_6)) {
    demo.crt = !demo.crt;
    apply_post(ctx);
  }
  if (key_pressed(ctx, key_f)) {
    particle_emitter &em = reg.get<particle_emitter>(demo.fountain);
    em.emitting = !em.emitting;
  }
  if (key_pressed(ctx, key_r)) {
    demo.rain_on = !demo.rain_on;
    reg.get<particle_emitter>(demo.rain).emitting = demo.rain_on;
  }
  if (key_pressed(ctx, key_space))
    explode(ctx, scr2w(ctx, mouse_pos(ctx)));
  if (key_pressed(ctx, key_equal))
    set_crowd(ctx, demo.crowd + crowd_step);
  if (key_pressed(ctx, key_minus))
    set_crowd(ctx, demo.crowd - crowd_step);
  if (key_pressed(ctx, key_tab))
    demo.hud = !demo.hud;
}

// The fountain stays at the hero's feet; the rain follows the camera.
void follow(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  reg.get<transform>(demo.fountain).pos = reg.get<transform>(demo.hero).pos;
  const vec2 view = screen_size(ctx) / camera_zoom;
  reg.get<transform>(demo.rain).pos = reg.get<transform>(demo.camera).pos - vec2{0.0f, view.y * 0.5f + 20.0f};
}

void draw_line(njin_ctx &ctx, const char *text, f32 y, rgba color = colors::white) {
  draw_text(ctx, text, {10.0f, y}, 16.0f, color);
}

void hud(njin_ctx &ctx) {
  demo.frame_ms += (delta_real(ctx) * 1000.0f - demo.frame_ms) * 0.05f;
  if (!demo.hud)
    return;
  const render_info r = render_info_get(ctx);
  entt::registry &reg = world(ctx);
  const bool fountain_on = reg.get<particle_emitter>(demo.fountain).emitting;

  char line[256];
  draw_rect(ctx, rect{{0.0f, 0.0f}, {screen_size(ctx).x, 108.0f}}, {0.0f, 0.0f, 0.0f, 0.62f});
  std::snprintf(line, sizeof line, "%.0f FPS   %.2f ms   vsync %s   entities %zu", 1000.0f / demo.frame_ms, demo.frame_ms,
                window_vsync(ctx) ? "on" : "off", reg.view<entt::entity>().size());
  draw_line(ctx, line, 6.0f);
  std::snprintf(line, sizeof line, "sprites %u drawn, %u off screen   tile chunks %u   draw calls ~%u   atlas %s", r.sprites,
                r.sprites_culled, r.tile_chunks, r.draw_calls, demo.use_atlas ? "ON" : "off");
  draw_line(ctx, line, 26.0f, r.draw_calls > 100 ? rgba{1.0f, 0.7f, 0.5f, 1.0f} : colors::white);
  std::snprintf(line, sizeof line, "particles %u (%u on the GPU, %u instanced calls)   emitters %u drawn, %u off screen   post passes %u",
                r.particles, r.particles_gpu, r.instanced_calls, r.emitters, r.emitters_culled, r.post_passes);
  draw_line(ctx, line, 46.0f);
  std::snprintf(line, sizeof line, "particles run on the %s%s", demo.gpu_particles && particles_gpu_available(ctx) ? "GPU" : "CPU",
                demo.gpu_particles && !particles_gpu_available(ctx) ? " (no GPU support found)" : "");
  draw_line(ctx, line, 66.0f, {0.6f, 0.9f, 1.0f, 1.0f});
  std::snprintf(line, sizeof line,
                "1 atlas  2 GPU/CPU particles  3 vsync  4 blur  5 bloom  6 CRT  F fountain (%s)  R rain  Space explode  +/- crowd %d  Tab hide",
                fountain_on ? "on" : "off", demo.crowd);
  draw_line(ctx, line, 86.0f, {0.8f, 0.85f, 0.95f, 1.0f});
}

void setup(njin_ctx &ctx) {
  ecs_register(ctx, phase_startup, startup, "startup");
  ecs_register(ctx, phase_update, input, "input");
  ecs_register(ctx, phase_post_update, follow, "follow");
  ecs_register(ctx, phase_post_render, hud, "hud");
}
} // namespace

int main() {
  njin::njin_ctx *ctx = njin::njin_create({.title = "njin render demo",
                                           .width = 1280,
                                           .height = 720,
                                           .target_fps = 240,
                                           .clear_bg_color = {0.05f, 0.07f, 0.06f, 1.0f}});
  njin::njin_mod_register(*ctx, {.name = "render_demo", .setup = setup});
  // Open for njin_inspector: the same numbers as the HUD, and more.
  njin::debug_server_start(*ctx);
  njin::njin_run(*ctx);
  njin::njin_destroy(ctx);
}

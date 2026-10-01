#include "city/city.h"
#include "game.h"
#include "kungfu_preview.h"
#include "person.h"
#include "types.h"

#include <algorithm>
#include <cstdio>

#include <cstdlib>
#include <string>

namespace {
bool clay_capture = false;
bool kungfu_mode = false;
int clay_frame = 0;
float clay_time = 0;
void clay_start(njin::context &ctx) { sandtable::person_init(ctx); }
void clay_render(njin::context &ctx) {
  using namespace njin;
  using namespace sandtable;
  ++clay_frame;
  clay_time += delta(ctx);
  const bool sheet = clay_capture && clay_frame >= 6 && clay_frame <= 11;
  const bool all_poses = clay_capture && clay_frame >= 36;
  const bool motion = clay_capture && clay_frame >= 12;
  begin_3d(ctx, {.position = all_poses ? vec3{.2f, 1.10f, 2.5f}
                             : sheet   ? vec3{.25f, .48f, 1.32f}
                                       : vec3{.25f, .27f, .64f},
                 .target = all_poses ? vec3{0, .14f, -.24f} : vec3{0, .16f, 0},
                 .fovy = 35,
                 .near_plane = .01f,
                 .entities = false});
  light3d_set(ctx, {.direction = {-.7f, -1.0f, -.4f},
                    .color = {.85f, .79f, .68f, 1},
                    .ambient = {.35f, .34f, .32f, 1},
                    .shadows = true,
                    .shadow_range = 1.5f,
                    .shadow_size = 2048,
                    .shadow_softness = 2.0f});
  material3d_set(ctx, {.specular = .03f});
  draw_plane3d(ctx, {0, -.001f, 0}, {20, 20}, rgb(213, 201, 178));
  if (all_poses) {
    for (int i = 0; i < static_cast<int>(act::count); ++i) {
      const act a = static_cast<act>(i);
      draw_person(ctx,
                  {.at = {(static_cast<f32>(i % 7) - 3.0f) * 11.0f,
                          -static_cast<f32>(i / 7) * 18.0f},
                   .facing = 90,
                   .now = a,
                   .time = act_duration(a) * (a == act::death ? 1.0f : .35f),
                   .tint = rgb(177, 68, 54)});
    }
  } else if (sheet) {
    const act actions[] = {act::idle, act::walk, act::sit, act::jab};
    const rgba colors[] = {rgb(177, 68, 54), rgb(72, 119, 91),
                           rgb(183, 135, 66), rgb(106, 83, 136)};
    for (int i = 0; i < 4; ++i)
      draw_person(ctx, {.at = {(static_cast<f32>(i) - 1.5f) * 10.0f, 0},
                        .facing = 90,
                        .now = actions[i],
                        .time = i == 3 ? .22f : .25f,
                        .tint = colors[i],
                        .identity = static_cast<u32>(i)});
  } else {
    draw_person(ctx, {.facing = 90,
                      .now = clay_capture && !motion ? act::talk : act::walk,
                      .time = motion ? static_cast<f32>(clay_frame - 12) / 24.0f
                              : clay_capture ? .8f
                                             : clay_time,
                      .tint = rgb(177, 68, 54)});
  }
  end_3d(ctx);
  if (clay_capture && clay_frame == 4)
    screenshot(ctx, "clay_sdf_closeup.png");
  if (clay_capture && clay_frame == 9)
    screenshot(ctx, "clay_sdf_poses.png");
  if (motion && clay_frame < 36) {
    char file[64];
    std::snprintf(file, sizeof(file), "clay_sdf_walk_%02d.png",
                  clay_frame - 12);
    screenshot(ctx, file);
  }
  if (clay_capture && clay_frame == 38)
    screenshot(ctx, "clay_sdf_all_actions.png");
  if (clay_capture && clay_frame == 40)
    quit(ctx);
}
void clay_setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, clay_start, "clay_start");
  njin::ecs_register(ctx, njin::phase_render, clay_render, "clay_render");
}
} // namespace

int main(int argc, char **argv) {
  using namespace njin;

  bool test_mode = false;
  bool clay_preview = false;
  u32 seed = 1;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const bool more = i + 1 < argc;
    if (arg == "--kungfu-test" || arg == "--kungfu-preview" ||
        arg == "--boxing-test" || arg == "--boxing-preview") {
      clay_preview = true;
      kungfu_mode = true;
      clay_capture = arg == "--kungfu-test" || arg == "--boxing-test";
    } else if (arg == "--clay-test" || arg == "--clay-preview") {
      clay_preview = true;
      clay_capture = arg == "--clay-test";
    } else if (arg == "--test" || arg == "-t")
      test_mode = true;
    else if (arg == "--seed" && more)
      seed = static_cast<u32>(std::strtoul(argv[++i], nullptr, 10));
    // --hour H: the town's clock starts at H (0 to 24); --speed S: how fast
    // it runs (the HUD offers 0, 1 and 3).
    else if (arg == "--hour" && more)
      sandtable::state.hour = std::clamp(static_cast<f32>(std::atof(argv[++i])), 0.0f, 23.99f);
    else if (arg == "--speed" && more)
      sandtable::state.speed = std::max(0.0f, static_cast<f32>(std::atof(argv[++i])));
    else if (arg == "--citycheck") {
      // --citycheck [first seed] [count]: generate and validate, no window.
      const u32 first =
          more ? static_cast<u32>(std::strtoul(argv[++i], nullptr, 10)) : 1u;
      const i32 count = i + 1 < argc ? std::atoi(argv[++i]) : 50;
      return sandtable::city::run_city_check(first, count, true) == 0 ? 0 : 1;
    } else if (arg == "--citymap" && i + 2 < argc) {
      // --citymap <seed> <file.ppm>: the raster of one city, a pixel a cell.
      sandtable::city::city_map map;
      sandtable::city::city_desc desc;
      desc.seed = static_cast<u32>(std::strtoul(argv[++i], nullptr, 10));
      sandtable::city::generate(map, desc);
      const bool ok = sandtable::city::write_city_ppm(map, argv[++i]);
      std::printf("[city] seed %u: %s\n", desc.seed,
                  ok ? "written" : "could not write");
      return ok ? 0 : 1;
    }
  }

  context *ctx = create({
      .title = "Sa Bàn Chiến Trận",
      .width = 1280.0f,
      .height = 720.0f,
      .target_fps = 60.0f,
      .clear_bg_color = clay_preview ? rgba{.80f, .75f, .64f, 1}
                                     : rgba{0.05f, 0.05f, 0.07f, 1.0f},
      .exit_key = key_none,
      .resizable = true,
      .app_name = "SandTable",
      // Drawn at the window's own resolution, table and HUD alike; the HUD's
      // sizes follow the window's height (view.h ui_scale).
  });

  mod_register(*ctx, kungfu_mode ? sandtable::kungfu_module(clay_capture)
                     : clay_preview
                         ? mod_desc{.name = "clay_preview", .setup = clay_setup}
                         : sandtable::module(test_mode, seed));

#ifndef NDEBUG
  if (!clay_preview)
    debug_server_start(*ctx);
#endif

  run(*ctx);
  destroy(ctx);
  return 0;
}

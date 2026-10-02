#include "kungfu_preview.h"
#include "person.h"
#include <cstdio>

namespace sandtable {
namespace {
bool retro_capture = false;
int retro_frame = 0;
f32 retro_time = 0;
void kungfu_render(njin::context &ctx) {
  using namespace njin;
  using namespace sandtable;
  ++retro_frame;
  retro_time += delta(ctx);
  const f32 clock =
      retro_capture ? static_cast<f32>(retro_frame - 1) / 24.0f : retro_time;
  const f32 t = std::fmod(clock, 2.0f);
  begin_3d(ctx, {.position = {.15f, 1.20f, 1.52f},
                 .target = {0, .14f, -.39f},
                 .fovy = 35,
                 .near_plane = .01f,
                 .entities = false});
  light3d_set(ctx, {.direction = {-.7f, -1.0f, -.4f},
                    .color = {.85f, .79f, .68f, 1},
                    .ambient = {.35f, .34f, .32f, 1},
                    .shadows = true,
                    .shadow_range = 1.8f,
                    .shadow_size = 2048,
                    .shadow_softness = 2.0f});
  material3d_set(ctx, {.specular = .03f});
  draw_plane3d(ctx, {0, -.001f, 0}, {20, 20}, rgb(213, 201, 178));
  const act moves[] = {act::boxing_combo,      act::boxing_hook,
                       act::boxing_front_kick, act::boxing_round_kick,
                       act::boxing_block,      act::boxing_low_kick};
  const rgba colors[] = {rgb(177, 68, 54),  rgb(72, 119, 91),
                         rgb(183, 135, 66), rgb(106, 83, 136),
                         rgb(58, 111, 143), rgb(135, 98, 63)};
  for (int i = 0; i < 6; ++i) {
    draw_person(ctx, {.at = {(static_cast<f32>(i % 3) - 1) * 13.0f,
                             -static_cast<f32>(i / 3) * 25.0f},
                      .facing = 55,
                      .now = moves[i],
                      .time = std::max(0.0f, t - .12f),
                      .tint = colors[i]});
  }
  end_3d(ctx);
  if (retro_capture) {
    char name[64];
    std::snprintf(name, sizeof(name), "boxing_%02d.png", retro_frame - 1);
    screenshot(ctx, name);
    if (retro_frame == 48)
      quit(ctx);
  }
}

void startup(context &ctx) { person_init(ctx); }
void setup(context &ctx) {
  ecs_register(ctx, phase_startup, startup, "kungfu_start");
  ecs_register(ctx, phase_render, kungfu_render, "kungfu_render");
}
} // namespace
mod_desc kungfu_module(bool capture) {
  retro_capture = capture;
  retro_frame = 0;
  retro_time = 0;
  return {.name = "kungfu_preview", .setup = setup};
}
} // namespace sandtable

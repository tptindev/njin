#include "city/city.h"
#include "game.h"
#include "person.h"

#include <cstdio>

#include <cstdlib>
#include <string>

namespace {
bool clay_capture=false;
int clay_frame=0;
float clay_time=0;
void clay_start(njin::context &ctx) { sandtable::person_init(ctx); }
void clay_render(njin::context &ctx) {
  using namespace njin;
  using namespace sandtable;
  ++clay_frame; clay_time+=delta(ctx);
  const bool sheet=clay_capture && clay_frame>=6;
  begin_3d(ctx,{.position=sheet?vec3{.6f,.65f,1.65f}:vec3{.30f,.30f,.78f},
               .target={0,.20f,0},.fovy=35,.near_plane=.01f,.entities=false});
  light3d_set(ctx,{.direction={-.7f,-1.0f,-.4f},.color={.85f,.79f,.68f,1},
                   .ambient={.35f,.34f,.32f,1},.shadows=true,.shadow_range=1.5f,
                   .shadow_size=2048,.shadow_softness=2.0f});
  material3d_set(ctx,{.specular=.03f});
  draw_plane3d(ctx,{0,-.001f,0},{20,20},rgb(213,201,178));
  if(sheet) {
    const act actions[]={act::idle,act::walk,act::sit,act::jab};
    const rgba colors[]={rgb(177,68,54),rgb(72,119,91),rgb(183,135,66),rgb(106,83,136)};
    for(int i=0;i<4;++i)
      draw_person(ctx,{.at={(static_cast<f32>(i)-1.5f)*10.0f,0},.facing=90,
        .now=actions[i],.time=i==3?.22f:.25f,.tint=colors[i],.identity=static_cast<u32>(i)});
  } else {
    draw_person(ctx,{.facing=90,.now=clay_capture?act::talk:act::walk,
                    .time=clay_capture?.8f:clay_time,.tint=rgb(177,68,54)});
  }
  end_3d(ctx);
  if(clay_capture && clay_frame==4) screenshot(ctx,"clay_sdf_closeup.png");
  if(clay_capture && clay_frame==9) screenshot(ctx,"clay_sdf_poses.png");
  if(clay_capture && clay_frame==12) quit(ctx);
}
void clay_setup(njin::context &ctx) {
  njin::ecs_register(ctx,njin::phase_startup,clay_start,"clay_start");
  njin::ecs_register(ctx,njin::phase_render,clay_render,"clay_render");
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
    if (arg == "--clay-test" || arg == "--clay-preview") {
      clay_preview=true;
      clay_capture=arg=="--clay-test";
    } else if (arg == "--test" || arg == "-t")
      test_mode = true;
    else if (arg == "--seed" && more)
      seed = static_cast<u32>(std::strtoul(argv[++i], nullptr, 10));
    else if (arg == "--citycheck") {
      // --citycheck [first seed] [count]: generate and validate, no window.
      const u32 first = more ? static_cast<u32>(std::strtoul(argv[++i], nullptr, 10)) : 1u;
      const i32 count = i + 1 < argc ? std::atoi(argv[++i]) : 50;
      return sandtable::city::run_city_check(first, count, true) == 0 ? 0 : 1;
    } else if (arg == "--citymap" && i + 2 < argc) {
      // --citymap <seed> <file.ppm>: the raster of one city, a pixel a cell.
      sandtable::city::city_map map;
      sandtable::city::city_desc desc;
      desc.seed = static_cast<u32>(std::strtoul(argv[++i], nullptr, 10));
      sandtable::city::generate(map, desc);
      const bool ok = sandtable::city::write_city_ppm(map, argv[++i]);
      std::printf("[city] seed %u: %s\n", desc.seed, ok ? "written" : "could not write");
      return ok ? 0 : 1;
    }
  }

  context *ctx = create({
      .title = "Sa Bàn Chiến Trận",
      .width = 1280.0f,
      .height = 720.0f,
      .target_fps = 60.0f,
      .clear_bg_color = {0.05f, 0.05f, 0.07f, 1.0f},
      .exit_key = key_none,
      .resizable = true,
      .app_name = "SandTable",
      // The HUD is laid out in a virtual screen of half the window (fit_view
      // keeps it so as the window changes) and drawn smooth at the window's
      // resolution; the 3D table is drawn at twice the virtual size, so at
      // the window's own resolution too.
      .virtual_size = {640.0f, 360.0f},
      .smooth_ui = true,
      .render_scale = 2,
  });

  mod_register(*ctx, clay_preview?mod_desc{.name="clay_preview",.setup=clay_setup}:sandtable::module(test_mode, seed));

#ifndef NDEBUG
  if (!clay_preview)
    debug_server_start(*ctx);
#endif

  run(*ctx);
  destroy(ctx);
  return 0;
}

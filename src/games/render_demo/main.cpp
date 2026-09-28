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
//   7  night: the frame goes dark and eight lights (a vec4 array) light it
//   8  dusk: brightness looked up in a colour ramp (a second texture)
//   9  haze: the frame wobbles by a noise texture (a third one)
//   L  lights: a lit scene (the frame is dark, lights and shadows add to it)
//   N  scene: night torches / a low sun with long shadows / a flashlight
//   O  shadows from trees, bushes and rocks on / off
//   B  shadow method: pixel-perfect (the alpha of the sprites) or polygons (shapes)
//   M  PBR maps (normal, roughness, metallic, occlusion) on the trees, bushes, rocks, hero and gold balls: on / off
//   P  falloff of point and spot lights: physical, linear, smooth, none
//   T  tonemap: shoulder, Reinhard, ACES (the flowers glow: an emissive map)
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
// handful, because sprites of one atlas page are one texture. Keys 7 to 9 run
// one shader over the whole frame (assets/scene.fs, camera_set_post_shader):
// shader_set_texture gives it the ramp and the noise, shader_set_vec4_array the
// lights. Keys L to P use the engine's 2D lighting (njin_light.h).
// njin_inspector (start it beside the game) shows the same numbers, per-system
// times and GPU memory.
#include "demo.h"

int main() {
  njin::njin_ctx *ctx = njin::njin_create({.title = "njin render demo",
                                           .width = 1280,
                                           .height = 720,
                                           .target_fps = 240,
                                           .clear_bg_color = {0.05f, 0.07f, 0.06f, 1.0f}});
  njin::njin_mod_register(*ctx, {.name = "render_demo", .setup = render_demo::setup});
  // Open for njin_inspector: the same numbers as the HUD, and more.
  njin::debug_server_start(*ctx);
  njin::njin_run(*ctx);
  njin::njin_destroy(ctx);
}

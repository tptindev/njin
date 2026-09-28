// A gallery of the 2D lighting (njin_light.h): twelve small rooms, one case in each, with what to look
// at and the lines of code that make it written on screen. Each room is built by one function in
// rooms_*.cpp, from nothing, so it reads as a complete example.
//
//   1  point lights: the four falloff curves
//   2  the size of a light: sharp and soft shadows
//   3  spot lights: cone, softness, turning with angle or with the transform
//   4  the sun: a directional light over a day, shadow_reach
//   5  the shapes of light_occluder: box, circle, ellipse, capsule, polygon, open line, hole
//   6  occluders that move, turn, grow and change their points
//   7  walls from a tilemap (light_occluders_from_tiles), a top-down hero carrying a torch
//   8  shadows from the sprites: light_occluder_sprite, light_occluder_pixels, a mask
//   9  PBR surfaces: normal maps, metallic, roughness, emissive; the light's height
//  10  colour: temperature, mixing, HDR, tonemap and exposure
//  11  many lights at once, with and without shadows
//  12  a side view platformer level: moonlight, lamp posts, a lantern
//
// Keys everywhere: Page Up / Page Down or F1..F12 change the room, L turns the lighting off to compare,
// G draws the outline of every occluder (what the lights actually see), H hides the text. Each room
// lists its own keys under its explanation.
#include "demo.h"

int main() {
  njin::njin_ctx *ctx = njin::njin_create({.title = "njin lighting demo",
                                           .width = 1280,
                                           .height = 720,
                                           .target_fps = 144,
                                           .clear_bg_color = {0.0f, 0.0f, 0.0f, 1.0f}});
  njin::njin_mod_register(*ctx, {.name = "lighting_demo", .setup = lighting_demo::setup});
  // Open for njin_inspector: the time of each lighting pass, per system.
  njin::debug_server_start(*ctx);
  njin::njin_run(*ctx);
  njin::njin_destroy(ctx);
}

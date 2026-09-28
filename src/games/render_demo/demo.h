// The state and the vocabulary the files of njin_render_demo share. Each .cpp owns one part of the demo:
//
//   module.cpp      startup, and the registration of the systems
//   world.cpp       the map, the crowd of trees and rocks, the hero, the images (atlas or not)
//   emitters.cpp    the fountain, the rain, the explosion
//   lights.cpp      the 2D lighting scenes (keys L, N, O, M, P)
//   frame_pass.cpp  the built-in post effects (4 to 6) and the whole-frame shader scene.fs (7 to 9)
//   input.cpp       the keys
//   hud.cpp         the text on top
#pragma once
#include <njin.h>

namespace render_demo {
using namespace njin;

// --- the map ---

constexpr vec2 tile{16.0f, 16.0f};
constexpr i32 map_w = 128;
constexpr i32 map_h = 96;
constexpr vec2 world_size{map_w * 16.0f, map_h * 16.0f};
constexpr i32 layer_ground = 0;
constexpr i32 layer_things = 1;
constexpr f32 camera_zoom = 2.0f;
constexpr i32 crowd_step = 500;

// --- the images ---

// One of the crowd's images.
enum kind : i32 { k_tree, k_bush, k_rock, k_flower, k_hero, k_spark, k_orb, kind_count };
constexpr const char *image_paths[kind_count] = {
    "assets/sprites/tree.png",   "assets/sprites/bush.png", "assets/sprites/rock.png",
    "assets/sprites/flower.png", "assets/sprites/hero.png", "assets/sprites/spark.png",
    "assets/sprites/orb.png"};
// The normal maps and the material maps (roughness, metallic, occlusion) of the images that have
// them (key M), made by tools/make_assets.py.
constexpr const char *normal_paths[kind_count] = {"assets/sprites/tree_n.png", "assets/sprites/bush_n.png",
                                                  "assets/sprites/rock_n.png", nullptr,
                                                  "assets/sprites/hero_n.png", nullptr,
                                                  "assets/sprites/orb_n.png"};
constexpr const char *material_paths[kind_count] = {"assets/sprites/tree_m.png", "assets/sprites/bush_m.png",
                                                    "assets/sprites/rock_m.png", nullptr,
                                                    "assets/sprites/hero_m.png", nullptr,
                                                    "assets/sprites/orb_m.png"};
constexpr const char *emissive_paths[kind_count] = {nullptr, nullptr, nullptr, "assets/sprites/flower_e.png",
                                                    nullptr, nullptr, nullptr};
// Where an image stands on the ground: bottom centre for all but the spark.
constexpr vec2 image_origin{0.5f, 1.0f};

// What kind of crowd member an entity is.
struct crowd_member {
  i32 kind = 0;
};

// --- the state ---

// The crowd's images, and how they are loaded (key 1: atlas or one by one; key M: normal maps).
struct image_state {
  atlas_handle atlas{};
  texture_handle packed[kind_count]{};   // the same images, in the atlas
  texture_handle separate[kind_count]{}; // and loaded one by one
  texture_handle packed_normal[kind_count]{};
  texture_handle separate_normal[kind_count]{};
  texture_handle packed_material[kind_count]{};
  texture_handle separate_material[kind_count]{};
  texture_handle packed_emissive[kind_count]{};
  texture_handle separate_emissive[kind_count]{};
  texture_handle trunk_mask{}; // only the trunk of a tree blocks light for the pixel shadows
  bool use_atlas = true;
  bool normal_maps = true; // and the material maps: key M
};

// The 2D lighting (keys L, N, O, P).
struct light_state {
  bool lit = false;
  bool shadows = true;
  i32 scene_preset = 0;
  i32 falloff = 0;
  i32 tonemap = 0; // key T: shoulder, Reinhard, ACES
  bool pixel_shadows = true; // key B: shadows from the pixels of the sprites, or from polygons
  entt::entity torch = entt::null;       // a light that follows the hero
  entt::entity mouse_light = entt::null; // a light at the mouse
  entt::entity flashlight = entt::null;  // a spot from the hero towards the mouse
};

// The whole-frame shader (assets/scene.fs). The switches are the keys 7 to 9; the amounts follow
// them so the frame fades instead of jumping.
struct pass_state {
  shader_handle shader{};
  bool night = false, dusk = false, haze = false;
  f32 night_amount = 0.0f, dusk_amount = 0.0f, haze_amount = 0.0f;
};

struct demo_state {
  image_state images;
  light_state lights;
  pass_state pass;
  bool gpu_particles = true;
  bool blur = false, bloom = false, crt = false; // built-in post effects, keys 4 to 6
  bool hud = true;
  bool rain_on = false;
  i32 crowd = 3000;
  entt::entity hero = entt::null;
  entt::entity camera = entt::null;
  entt::entity fountain = entt::null;
  entt::entity rain = entt::null;
  f32 frame_ms = 16.0f; // smoothed
};

// The one instance.
extern demo_state demo;

// --- world.cpp ---

// Loads every image and normal map, in the atlas and on its own.
void load_images(njin_ctx &ctx);
// The image of a kind, from the atlas or loaded on its own, and its normal map (empty when off).
texture_handle image(i32 which);
texture_handle image_normal(i32 which);
texture_handle image_material(i32 which);
texture_handle image_emissive(i32 which);
void build_map(njin_ctx &ctx);
void build_hero(njin_ctx &ctx);
// Makes or removes crowd members until there are `wanted`.
void set_crowd(njin_ctx &ctx, i32 wanted);
// Points every sprite at the images of the current setting (keys 1 and M).
void apply_atlas(njin_ctx &ctx);
void apply_normals(njin_ctx &ctx);
// What blocks light on one crowd member, by the current shadow method (key B).
void set_occluder(entt::registry &reg, entt::entity e, i32 which);
// Gives every tree, bush and rock the occluders of the current shadow method (key B).
void apply_occluders(njin_ctx &ctx);
// Scatters gold balls over the map, to see metal in the light.
void build_orbs(njin_ctx &ctx);

// --- emitters.cpp ---

void build_emitters(njin_ctx &ctx);
void explode(njin_ctx &ctx, vec2 at);
// A system: the fountain stays at the hero's feet, the rain follows the camera.
void follow_emitters(njin_ctx &ctx);

// --- lights.cpp ---

constexpr i32 scene_count = 3;
const char *scene_name(i32 preset);
const char *falloff_name(i32 falloff);
const char *tonemap_name(i32 tonemap);
// Sets up the lights of the current scene from scratch.
void build_lights(njin_ctx &ctx);
// A system: the lights that follow the hero and the mouse, and the two settings the keys flip.
void update_lights(njin_ctx &ctx);

// --- frame_pass.cpp ---

// Sends the keys 4 to 6 to the engine's post effects.
void apply_post(njin_ctx &ctx);
void load_frame_pass(njin_ctx &ctx);
// A system: fades the amounts and sets the uniforms of scene.fs before the world is drawn.
void update_frame_pass(njin_ctx &ctx);

// --- systems ---

void input(njin_ctx &ctx);
void hud(njin_ctx &ctx);
void setup(njin_ctx &ctx); // registers everything for njin_mod_register
} // namespace render_demo

// The state and the vocabulary the files of njin_lighting_demo share. The demo is a gallery: a row of
// rooms, one case of the 2D lighting (njin_light.h) in each, with what to look at written on screen.
// Only the current room exists: going to another one destroys every entity of this one and builds the
// next from scratch, so each room's build function is a complete, self-contained example.
//
//   module.cpp          startup, the systems, going from room to room, the keys every room shares
//   common.cpp          the images, the floor and walls, spawning lights and occluders, labels
//   rooms_lights.cpp    rooms 1 to 4: the kinds of light and what their settings do
//   rooms_occluders.cpp rooms 5 to 8: what casts shadows
//   rooms_surfaces.cpp  rooms 9 to 12: materials, colour, many lights, a side view level
//   hud.cpp             the text on top, the labels, the occluder outlines (key G)
#pragma once
#include <njin.h>

#include <cstdio>
#include <string>
#include <vector>

namespace lighting_demo {
using namespace njin;

// --- the room ---

// Every room is built in the same box of the world, the size of the view: the camera never moves.
constexpr vec2 room_size{640.0f, 360.0f};
constexpr vec2 room_centre{320.0f, 180.0f};
constexpr vec2 tile{16.0f, 16.0f};
constexpr f32 camera_zoom = 2.0f;
// The top of the screen holds the explanation; the rooms keep their content below this line.
constexpr f32 room_top = 64.0f;

constexpr i32 layer_floor = 0;
constexpr i32 layer_walls = 1;
constexpr i32 layer_things = 2;

// The tiles of assets/tiles.png (tools/make_assets.py).
enum tile_id : i32 {
  t_stone,
  t_stone2,
  t_wood,
  t_grass,
  t_brick,
  t_dirt_top,
  t_dirt,
  t_back,
};

// One room of the gallery.
struct room {
  const char *title;
  // What the room shows and what to look at, a line per `\n`.
  const char *what;
  // The lines of code that matter, shown under the explanation.
  const char *code;
  // The room's own keys, or empty.
  const char *keys;
  void (*build)(njin_ctx &ctx);  // makes the room's entities and sets lighting_desc
  void (*update)(njin_ctx &ctx); // every frame, may be null
  void (*draw)(njin_ctx &ctx);   // in the world, under the lighting, may be null
};

// A tag on every entity a room makes, so leaving the room can destroy them all.
struct room_entity {};

// A line of text under something in the world, drawn on top of the lit image so it is always readable.
struct label {
  vec2 at; // world position of the text's top centre
  std::string text;
  rgba color{0.92f, 0.94f, 1.0f, 1.0f};
};

// --- the images ---

struct images {
  texture_handle tiles{};
  texture_handle tree{}, tree_n{}, tree_trunk{};
  texture_handle rock{}, rock_n{}, rock_m{};
  texture_handle crate{}, crate_n{}, crate_m{};
  texture_handle ball{}, ball_n{}, ball_m[4]{}; // rough, glossy, brushed metal, mirror
  texture_handle gold{};
  texture_handle crystal{}, crystal_n{}, crystal_e{};
  texture_handle hero{}, hero_n{}, hero_m{};
  texture_handle lamp{}, lamp_e{};
};

// --- the state ---

struct demo_state {
  images img;
  i32 current = 0;
  bool lit = true;        // key L: the lighting on, or the plain picture
  bool outlines = false;  // key G: draw the occluders' shapes on top
  bool help = true;       // key H: the text on top
  entt::entity camera = entt::null;
  std::vector<label> labels;
  f32 frame_ms = 16.0f; // smoothed
};

extern demo_state demo;

// --- common.cpp ---

void load_images(njin_ctx &ctx);
// A new entity of the current room, with a transform.
entt::entity spawn(njin_ctx &ctx, vec2 pos, f32 rot = 0.0f, f32 scale = 1.0f);
// A light at `pos`.
entt::entity add_light(njin_ctx &ctx, vec2 pos, const light_2d &light);
// A shape that blocks light at `pos`.
entt::entity add_occluder(njin_ctx &ctx, vec2 pos, light_occluder shape, f32 rot = 0.0f);
// A sprite standing on the ground at `pos` (its bottom centre).
entt::entity add_sprite(njin_ctx &ctx, vec2 pos, texture_handle texture, texture_handle normal = {},
                        texture_handle material = {});
// Covers the room with floor tile `id` (two ids alternate in a checker when `id2` >= 0).
entt::entity fill_floor(njin_ctx &ctx, i32 id, i32 id2 = -1);
// A tilemap of walls from rows of text ('#' is a wall) at the room's origin: drawn, solid for bodies, and
// its outlines made into occluders (light_occluders_from_tiles). Returns the tilemap's entity.
entt::entity build_walls(njin_ctx &ctx, std::initializer_list<std::string_view> rows, i32 wall_tile = t_brick);
// Rebuilds the occluders of the wall tilemap `walls` after its tiles changed.
void rebuild_wall_occluders(njin_ctx &ctx, entt::entity walls);
// The lighting settings every room starts from: on (unless key L turned it off), the night ambient.
lighting_desc base_lighting();
void add_label(vec2 at, std::string text, rgba color = {0.92f, 0.94f, 1.0f, 1.0f});
// The mouse, in the world.
vec2 mouse_world(njin_ctx &ctx);
// Draws the filled shape of an occluder (a convex or star shaped polygon, around its first point's centre)
// in the world, so there is something to see where the shadow comes from.
void draw_occluder_shape(njin_ctx &ctx, const light_occluder &o, const transform &t, rgba fill, rgba edge);

// printf into a std::string, for the labels that show a value.
template <typename... A> std::string fmt(const char *format, A... args) {
  char buf[200];
  std::snprintf(buf, sizeof buf, format, args...);
  return buf;
}

// --- rooms ---

extern const room rooms_lights[4];
extern const room rooms_occluders[4];
extern const room rooms_surfaces[4];
constexpr i32 room_count = 12;
const room &room_at(i32 index);

// --- hud.cpp ---

void hud(njin_ctx &ctx);
// In the world: the current room's own drawing, and the occluder outlines of key G.
void draw_world(njin_ctx &ctx);

void setup(njin_ctx &ctx);
} // namespace lighting_demo
